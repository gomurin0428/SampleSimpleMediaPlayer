#pragma comment (lib, "Shlwapi.lib")
#include "CPlayer.h"
#include "shlwapi.h"
#include <new>
#include <cassert>
#include "Mfidl.h"
#include "evr.h"
#include "Mfapi.h"

int easyPlay(HWND hVideo, HWND hEvent) {
    CPlayer* player = NULL;
    HRESULT result = CPlayer::CreateInstance(hVideo, hEvent,&player);
    player->OpenURL(L"test.mp4");
    player->StartPlayback();
    return result;
}

// IUnknown メソッド
HRESULT CPlayer::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] =
    {
        QITABENT(CPlayer, IMFAsyncCallback),
        { 0 }
    };
    return QISearch(this, qit, riid, ppv);
}

ULONG CPlayer::AddRef()
{
    return InterlockedIncrement(&m_nRefCount);
}

ULONG CPlayer::Release()
{
    ULONG uCount = InterlockedDecrement(&m_nRefCount);
    if (uCount == 0)
    {
        delete this;
    }
    return uCount;
}

// CPlayerオブジェクトを作成する、静的クラスメソッド
HRESULT CPlayer::CreateInstance(
    HWND hWndVideo,
    HWND hWndEvent,     // 通知を受け取るウィンドウ
    CPlayer** ppPlayer) // CPlayerオブジェクトへのポインタを受け取るためのポインタ
{
    if (ppPlayer == NULL)
    {
        return E_POINTER;
    }

    // CPlayerオブジェクトを作成する
    CPlayer* pPlayer = new (std::nothrow) CPlayer(hWndVideo, hWndEvent);
    if (pPlayer == NULL)
    {
        return E_OUTOFMEMORY;
    }

    // 初期化する
    HRESULT hr = pPlayer->Initialize();
    if (SUCCEEDED(hr))
    {
        *ppPlayer = pPlayer;
    }
    else
    {
        pPlayer->Release();
    }
    return hr;
}

HRESULT CPlayer::Initialize()
{
    // Media Foundationプラットフォームの開始
    HRESULT hr = MFStartup(MF_VERSION);
    if (SUCCEEDED(hr))
    {
        m_hCloseEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (m_hCloseEvent == NULL)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
    }
    return hr;
}

CPlayer::CPlayer(HWND hWndVideo, HWND hWndEvent) :
    m_pMediaSession(NULL),
    m_pMediaSource(NULL),
    m_pVideoDisplayControl(NULL),
    m_hWndVideo(hWndVideo),
    m_hWndEvent(hWndEvent),
    m_playerState(Closed),
    m_hCloseEvent(NULL),
    m_nRefCount(1)
{
}

CPlayer::~CPlayer()
{
    assert(m_pMediaSession == NULL);
    // FALSEならば、アプリケーションはShutdown()を呼ばなかった。

    // CPlayerがMedia SessionでIMediaEventGenerator::BeginGetEventを呼ぶとき、
    // Media SessionはCPlayerの参照カウントを保持する

    // これはCPlayerとMedia Sessionの間の循環参照カウントを作成する。
    // Shutdownの呼び出しは循環参照カウントを破壊する。

    // もしCreateInstanceが失敗ならば、アプリケーションはShutdownを呼ばない。
    // その場合は、デストラクタでShutdownを呼ぶ。

    Shutdown();
}

// 再生用のURLを開く
HRESULT CPlayer::OpenURL(const WCHAR* sURL)
{
    // 1. 新しいMedia Sessionを作成する
    // 2. Media Sourceを作成する
    // 3. Topologyを作成する
    // 4. Topologyをキューに登録する [非同期]
    // 5. 再生を開始する [非同期 - このメソッドでは起こらない]
    //  (MESessionTopologyStatusイベントをHandleEvent()で捕捉し、OnTopologyStatus()からStartPlayback()が呼ばれることで再生が開始される)

    IMFPresentationDescriptor* pSourcePD = NULL;
    IMFTopology* pTopology = NULL;

    // 1. Media Sessionを作成する
    HRESULT hr = CreateSession();
    if (FAILED(hr)) goto done;

    // 2. Media Sourceを作成する
    hr = CreateMediaSource(sURL, &m_pMediaSource);
    if (FAILED(hr)) goto done;

    // Media Source用のプレゼンテーション記述子 (presentation descriptor) を作成する
    hr = m_pMediaSource->CreatePresentationDescriptor(&pSourcePD);
    if (FAILED(hr)) goto done;

    // 3. 部分Topology (partial topology) を作成する
    hr = CreatePlaybackTopology(m_pMediaSource, pSourcePD, m_hWndVideo, &pTopology);
    if (FAILED(hr)) goto done;

    // 4. Media SessionにTopologyを設定する
    hr = m_pMediaSession->SetTopology(0, pTopology);
    if (FAILED(hr)) goto done;

    m_playerState = OpenPending;

    // SetTopologyが成功したならば、Media SessionはMESessionTopologySetイベントをキューに登録する

done:
    if (FAILED(hr))
    {
        m_playerState = Closed;
    }

    SafeRelease(&pSourcePD);
    SafeRelease(&pTopology);
    return hr;
}

// URLからMedia Sourceを作成する
HRESULT CPlayer::CreateMediaSource(PCWSTR sourceURL, IMFMediaSource** ppMediaSource)
{
    IMFSourceResolver* pSourceResolver = NULL;
    IUnknown* pMediaSource = NULL;

    // Source Resolverを作成する
    HRESULT hr = MFCreateSourceResolver(&pSourceResolver);

    MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;
    if (FAILED(hr)) goto done;

    // Source Resolverを使用して、Media Sourceを作成する

    // 注：簡単のために、このサンプルではMedia Sourceの作成に同期メソッドを用いている。
    // しかしながらMedia Sourceの作成は、たくさんの時間を要する。特にネットワーク ソースの場合は。
    // より応答性の良いUIのためには、非同期のBeginCreateObjectFromURLメソッドを用いる。

    hr = pSourceResolver->CreateObjectFromURL(
        sourceURL,
        MF_RESOLUTION_MEDIASOURCE, // ソース オブジェクトの作成
        NULL,         // 省略可能なプロパティ ストア
        &ObjectType,  // 作成されたオブジェクト型を受け取るポインタ
        &pMediaSource // Media Sourceへのポインタを受け取るポインタ
    );
    if (FAILED(hr)) goto done;

    // Media SourceからIMFMediaSourceインターフェイスを取得する
    hr = pMediaSource->QueryInterface(IID_PPV_ARGS(ppMediaSource));

done:
    SafeRelease(&pSourceResolver);
    SafeRelease(&pMediaSource);
    return hr;
}

// Media Sourceから再生Topologyを作成する
HRESULT CPlayer::CreatePlaybackTopology(
    IMFMediaSource* pMediaSource,
    IMFPresentationDescriptor* pPD,
    HWND hVideoWnd,
    IMFTopology** ppTopology)
{
    IMFTopology* pTopology = NULL;


    // Media Sourceにあるストリームの数を取得する
    DWORD dwSourceStreamsCount = 0;

    // 新しいTopologyの作成
    HRESULT hr = MFCreateTopology(&pTopology);
    if (FAILED(hr)) goto done;

    hr = pPD->GetStreamDescriptorCount(&dwSourceStreamsCount);
    if (FAILED(hr)) goto done;

    // ストリームごとにTopologyノードを作成し、それをTopologyへ追加する
    for (DWORD i = 0; i < dwSourceStreamsCount; i++)
    {
        hr = AddBranchToPartialTopology(pTopology, pMediaSource, pPD, i, hVideoWnd);
        if (FAILED(hr)) goto done;
    }

    // 呼び出し元へIMFTopologyポインタを返す
    *ppTopology = pTopology;
    (*ppTopology)->AddRef();

done:
    SafeRelease(&pTopology);
    return hr;
}

// 1つのストリーム用に、Topologyブランチを追加する
//
// ストリームごとに、この関数は次のように処理する
//
// 1. ストリームに関連するソース ノードを作成する
// 2. レンダラ用の出力ノードを作成する
// 3. 2つのノードを接続する
//
// Media Sessionは必要なデコーダを追加する
HRESULT CPlayer::AddBranchToPartialTopology(
    IMFTopology* pTopology,
    IMFMediaSource* pMediaSource,
    IMFPresentationDescriptor* pPD,
    DWORD dwStreamIndex,
    HWND hVideoWnd)
{
    IMFStreamDescriptor* pSD = NULL;
    IMFActivate* pSinkActivate = NULL;
    IMFTopologyNode* pSourceNode = NULL;
    IMFTopologyNode* pOutputNode = NULL;

    BOOL fSelected = FALSE;

    HRESULT hr = pPD->GetStreamDescriptorByIndex(dwStreamIndex, &fSelected, &pSD);
    if (FAILED(hr)) goto done;

    if (fSelected)
    {
        // メディア シンク アクティブ化オブジェクトを作成する
        hr = CreateMediaSinkActivate(pSD, hVideoWnd, &pSinkActivate);
        if (FAILED(hr)) goto done;

        // このストリームに、ソース ノードを追加する
        hr = AddSourceNode(pTopology, pMediaSource, pPD, pSD, &pSourceNode);
        if (FAILED(hr)) goto done;

        // レンダラ用の、出力ノードを作成する
        hr = AddOutputNode(pTopology, pSinkActivate, 0, &pOutputNode);
        if (FAILED(hr)) goto done;

        // ソースノードを出力ノードに接続する
        hr = pSourceNode->ConnectOutput(0, pOutputNode, 0);
    }
    // else: 選択されていなければ、ブランチを追加しない

done:
    SafeRelease(&pSD);
    SafeRelease(&pSinkActivate);
    SafeRelease(&pSourceNode);
    SafeRelease(&pOutputNode);
    return hr;
}

// ストリーム メディアタイプを基礎とする、レンダラ用のアクティブ化オブジェクト作成する
HRESULT CPlayer::CreateMediaSinkActivate(
    IMFStreamDescriptor* pSourceSD,
    HWND hVideoClippingWindow,
    IMFActivate** ppActivate)
{
    IMFMediaTypeHandler* pMediaTypeHandler = NULL;
    IMFActivate* pActivate = NULL;

    // ストリーム用のメディアタイプ ハンドラを取得する
    HRESULT hr = pSourceSD->GetMediaTypeHandler(&pMediaTypeHandler);
    if (FAILED(hr)) goto done;

    // メジャーメディアタイプを取得する
    GUID guidMajorType;
    hr = pMediaTypeHandler->GetMajorType(&guidMajorType);
    if (FAILED(hr)) goto done;

    // メディアタイプを基礎とする、レンダラ用のIMFActivateオブジェクトを作成する
    if (MFMediaType_Audio == guidMajorType)
    {
        // 音声レンダラを作成する
        hr = MFCreateAudioRendererActivate(&pActivate);
    }
    else if (MFMediaType_Video == guidMajorType)
    {
        // 映像レンダラを作成する
        hr = MFCreateVideoRendererActivate(hVideoClippingWindow, &pActivate);
    }
    else
    {
        // 未知のストリームタイプ
        hr = E_FAIL;
        // 任意だが、失敗の代わりにこのストリームを選択解除できる
    }
    if (FAILED(hr)) goto done;

    // 呼び出し元へIMFActivateポインタを返す
    *ppActivate = pActivate;
    (*ppActivate)->AddRef();

done:
    SafeRelease(&pMediaTypeHandler);
    SafeRelease(&pActivate);
    return hr;
}

// Topologyへソースノードを追加する
HRESULT CPlayer::AddSourceNode(
    IMFTopology* pTopology,
    IMFMediaSource* pMediaSource,
    IMFPresentationDescriptor* pPD,
    IMFStreamDescriptor* pSD,
    IMFTopologyNode** ppSourceNode)
{
    IMFTopologyNode* pNode = NULL;

    // ノードの作成
    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &pNode);
    if (FAILED(hr)) goto done;

    // 属性の設定
    hr = pNode->SetUnknown(MF_TOPONODE_SOURCE, pMediaSource);
    if (FAILED(hr)) goto done;

    hr = pNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pPD);
    if (FAILED(hr)) goto done;

    hr = pNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, pSD);
    if (FAILED(hr)) goto done;

    // Topologyへノードを追加する
    hr = pTopology->AddNode(pNode);
    if (FAILED(hr)) goto done;

    // 呼び出し元へポインタを返す
    *ppSourceNode = pNode;
    (*ppSourceNode)->AddRef();

done:
    SafeRelease(&pNode);
    return hr;
}

// Topologyへ出力ノードを追加する
HRESULT CPlayer::AddOutputNode(
    IMFTopology* pTopology,
    IMFActivate* pMediaSinkActivate,
    DWORD dwStreamSinkId,
    IMFTopologyNode** ppOutputNode)
{
    IMFTopologyNode* pNode = NULL;

    // ノードを作成する
    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pNode);
    if (FAILED(hr)) goto done;

    // オブジェクト ポインタを設定する
    hr = pNode->SetObject(pMediaSinkActivate);
    if (FAILED(hr)) goto done;

    // ストリーム シンクID属性を設定する
    hr = pNode->SetUINT32(MF_TOPONODE_STREAMID, dwStreamSinkId);
    if (FAILED(hr)) goto done;

    hr = pNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE);
    if (FAILED(hr)) goto done;

    // Topologyへノードを追加する
    hr = pTopology->AddNode(pNode);
    if (FAILED(hr)) goto done;

    // 呼び出し元へポインタを返す
    *ppOutputNode = pNode;
    (*ppOutputNode)->AddRef();

done:
    SafeRelease(&pNode);
    return hr;
}

// Media Sessionの新しいインスタンスを作成する
HRESULT CPlayer::CreateSession()
{
    // もしあれば、古いセッションを閉じる
    HRESULT hr = CloseSession();
    if (FAILED(hr)) goto done;

    assert(m_playerState == Closed);

    // Media Sessionを作成する
    hr = MFCreateMediaSession(NULL, &m_pMediaSession);
    if (FAILED(hr)) goto done;

    //// Media Sessionからのイベントのプルを開始する
    //// このメソッドは非同期であり、呼び出しは即座に返される。
    //// 次のSessionイベントが発生したときに、Media SessionはIMFAsyncCallback::Invokeを呼び出す。
    //hr = m_pMediaSession->BeginGetEvent((IMFAsyncCallback*)this, NULL);
    //if (FAILED(hr)) goto done;

    m_playerState = Ready;

done:
    return hr;
}

// 非同期のBeginGetEventメソッド用のコールバック
HRESULT CPlayer::Invoke(IMFAsyncResult* pAsyncResult)
{
    // このメソッドはワーカースレッドで呼ばれる。

    MediaEventType mediaEventType = MEUnknown;
    IMFMediaEvent* pMediaEvent = NULL;

    // イベント キューからイベントを取得する
    HRESULT hr = m_pMediaSession->EndGetEvent(pAsyncResult, &pMediaEvent);
    if (FAILED(hr)) goto done;

    // イベントの種類を取得する
    hr = pMediaEvent->GetType(&mediaEventType);
    if (FAILED(hr)) goto done;


    if (mediaEventType == MESessionClosed) // セッションは閉じられた。
    {
        // アプリケーションは m_hCloseEventイベントがハンドルされるのを待機している。
        SetEvent(m_hCloseEvent);
    }
    else
    {
        // すべての他のイベントのために、キューの次のイベントを取得する
        hr = m_pMediaSession->BeginGetEvent(this, NULL);
        if (FAILED(hr)) goto done;
    }

    // アプリケーションの状態を確認する

    // もしIMFMediaSession::Close の呼び出しが未解決ならば、アプリケーションが
    // m_hCloseEvent イベントを待機し、アプリケーションのメッセージループがブロックされている。
    // さもなくばプライベート ウィンドウ メッセージをアプリケーションへ送る。

    if (m_playerState != Closing)
    {
        // イベントに参照カウントを残す
        pMediaEvent->AddRef();

        // WM_APP_PLAYER_EVENTメッセージをアプリケーションへ送る
        PostMessage(m_hWndEvent, WM_APP_PLAYER_EVENT, (WPARAM)pMediaEvent, (LPARAM)mediaEventType);
    }

done:
    SafeRelease(&pMediaEvent);
    return S_OK;
}

// このメソッドは、WM_APP_PLAYER_EVENTメッセージを受け取ったアプリケーションから呼び出される
HRESULT CPlayer::HandleEvent(UINT_PTR pMediaEventPtr)
{
    IMFMediaEvent* pMediaEvent = (IMFMediaEvent*)pMediaEventPtr;
    if (pMediaEvent == NULL)
    {
        return E_POINTER;
    }

    // イベントの種類を取得する
    MediaEventType mediaEventType = MEUnknown;
    HRESULT hr = pMediaEvent->GetType(&mediaEventType);

    HRESULT hrStatus = S_OK;
    if (FAILED(hr)) goto done;

    // イベントの状態を取得する
    // イベントのきっかけとなった操作が成功しなかったならば、状態は失敗コードとなる
    hr = pMediaEvent->GetStatus(&hrStatus);

    // 非同期操作が成功したか確認する
    if (SUCCEEDED(hr) && FAILED(hrStatus))
    {
        hr = hrStatus;
    }
    if (FAILED(hr)) goto done;

    switch (mediaEventType)
    {
    case MESessionTopologyStatus: // Topologyの状態が変化した
        hr = OnTopologyStatus(pMediaEvent);
        break;

    case MEEndOfPresentation: // プレゼンテーションが終了した (再生がファイルの最後に到達した)
        hr = OnPresentationEnded(pMediaEvent);
        break;

    case MENewPresentation: // 新しいプレゼンテーションが用意できた
        hr = OnNewPresentation(pMediaEvent);
        break;

    default:
        hr = OnSessionEvent(pMediaEvent, mediaEventType);
        break;
    }

done:
    SafeRelease(&pMediaEvent);
    return hr;
}

HRESULT CPlayer::OnTopologyStatus(IMFMediaEvent* pMediaEvent)
{
    UINT32 status;

    HRESULT hr = pMediaEvent->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, &status);
    if (SUCCEEDED(hr) && (status == MF_TOPOSTATUS_READY))
    {
        SafeRelease(&m_pVideoDisplayControl);

        // EVRからIMFVideoDisplayControl インターフェイスを取得する。
        // この呼び出しは、メディアファイルに映像ストリームがないときは失敗することが予期される。

        //hr = MFGetService(m_pMediaSession, MR_VIDEO_RENDER_SERVICE, IID_PPV_ARGS(&m_pVideoDisplayControl));

        hr = StartPlayback();
    }
    return hr;
}

// MEEndOfPresentationイベント用のハンドラ
HRESULT CPlayer::OnPresentationEnded(IMFMediaEvent* pMediaEvent)
{
    // セッションは自動的に自身を、停止状態にする
    m_playerState = Stopped;
    return S_OK;
}

template <class Q>
HRESULT GetEventObject(IMFMediaEvent* pMediaEvent, Q** ppObject)
{
    *ppObject = NULL; // zero output

    PROPVARIANT var;
    HRESULT hr = pMediaEvent->GetValue(&var);
    if (SUCCEEDED(hr))
    {
        if (var.vt == VT_UNKNOWN)
        {
            hr = var.punkVal->QueryInterface(ppObject);
        }
        else
        {
            hr = MF_E_INVALIDTYPE;
        }
        PropVariantClear(&var);
    }
    return hr;
}

// MENewPresentation用のハンドラ
//
// このイベントは、Media Sourceに新しいTopologyを要求する新しいプレゼンテーションがあるならば送られる
HRESULT CPlayer::OnNewPresentation(IMFMediaEvent* pMediaEvent)
{
    IMFPresentationDescriptor* pPD = NULL;
    IMFTopology* pTopology = NULL;

    // イベントからプレゼンテーション記述子を取得する
    HRESULT hr = GetEventObject(pMediaEvent, &pPD);
    if (FAILED(hr)) goto done;

    // 部分Topologyを作成する
    hr = CreatePlaybackTopology(m_pMediaSource, pPD, m_hWndVideo, &pTopology);
    if (FAILED(hr)) goto done;

    // Media Sessionに、Topologyを設定する
    hr = m_pMediaSession->SetTopology(0, pTopology);
    if (FAILED(hr)) goto done;

    m_playerState = OpenPending;

done:
    SafeRelease(&pTopology);
    SafeRelease(&pPD);
    return S_OK;
}



// 現在位置から、再生を開始する
HRESULT CPlayer::StartPlayback()
{
    assert(m_pMediaSession != NULL);

    PROPVARIANT varStart;
    PropVariantInit(&varStart);

    HRESULT hr = m_pMediaSession->Start(&GUID_NULL, &varStart);
    if (SUCCEEDED(hr))
    {
        // 注：開始は非同期操作。しかしながら、すでに開始していると見なせる。
        // もし開始が後で失敗するならば、エラーコードと共にMESessionStartedイベントを受け取るため、
        // そのとき状態を更新する。
        m_playerState = Started;
    }

    PropVariantClear(&varStart);
    return hr;
}

// 一時停止または停止から、再生を開始する
HRESULT CPlayer::Play()
{
    if (m_playerState != Paused && m_playerState != Stopped)
    {
        return MF_E_INVALIDREQUEST;
    }
    if (m_pMediaSession == NULL || m_pMediaSource == NULL)
    {
        return E_UNEXPECTED;
    }
    return StartPlayback();
}

// 再生の一時停止
HRESULT CPlayer::Pause()
{
    if (m_playerState != Started)
    {
        return MF_E_INVALIDREQUEST;
    }
    if (m_pMediaSession == NULL || m_pMediaSource == NULL)
    {
        return E_UNEXPECTED;
    }

    HRESULT hr = m_pMediaSession->Pause();
    if (SUCCEEDED(hr))
    {
        m_playerState = Paused;
    }
    return hr;
}

// 再生の停止
HRESULT CPlayer::Stop()
{
    if (m_playerState != Started && m_playerState != Paused)
    {
        return MF_E_INVALIDREQUEST;
    }
    if (m_pMediaSession == NULL)
    {
        return E_UNEXPECTED;
    }

    HRESULT hr = m_pMediaSession->Stop();
    if (SUCCEEDED(hr))
    {
        m_playerState = Stopped;
    }
    return hr;
}

// 映像ウィンドウの再描画 (このメソッドはWM_PAINTで呼ぶ)
//HRESULT CPlayer::Repaint()
//{
//    if (m_pVideoDisplayControl)
//    {
//        return m_pVideoDisplayControl->RepaintVideo();
//    }
//    else
//    {
//        return S_OK;
//    }
//}
//
//// WM_PAINT メッセージのハンドラ
//void OnPaint(HWND hwnd)
//{
//    PAINTSTRUCT ps;
//    HDC hdc = BeginPaint(hwnd, &ps);
//
//    if (g_pPlayer && g_pPlayer->HasVideo())
//    {
//        // 動画は再生中。プレーヤに再描画を求める
//        g_pPlayer->Repaint();
//    }
//    else
//    {
//        // 動画が再生されていないため、アプリケーション ウィンドウを描画しなければならない
//        RECT rc;
//        GetClientRect(hwnd, &rc);
//        FillRect(hdc, &rc, (HBRUSH)COLOR_WINDOW);
//    }
//    EndPaint(hwnd, &ps);
//}

// 映像の矩形領域のサイズ変更
// (映像ウィンドウのサイズが変更されたならば、このメソッドを呼ぶ)
HRESULT CPlayer::ResizeVideo(WORD width, WORD height)
{
    if (m_pVideoDisplayControl)
    {
        // 目的の矩形を設定する。映像と同サイズとするならば (0,0,1,1) と設定する。
        RECT rcDest = { 0, 0, width, height };
        return m_pVideoDisplayControl->SetVideoPosition(NULL, &rcDest);
    }
    else
    {
        return S_OK;
    }
}

// Media Sessionを閉じる
HRESULT CPlayer::CloseSession()
{
    // IMFMediaSession::Close メソッドは非同期だが、
    // CPlayer::CloseSession メソッドはMESessionClosed イベントを待機する。
    //
    // MESessionClosed はMedia Sessionが引き起こした最後のイベントであることが保証される。

    HRESULT hr = S_OK;

    SafeRelease(&m_pVideoDisplayControl);

    // 最初にMedia Sessionを閉じる
    if (m_pMediaSession)
    {
        m_playerState = Closing;

        hr = m_pMediaSession->Close();
        if (SUCCEEDED(hr))
        {
            // 閉じる操作の完了を待つ
            DWORD dwTimeoutMS = 5000;
            DWORD dwWaitResult = WaitForSingleObject(m_hCloseEvent, dwTimeoutMS);
            if (dwWaitResult == WAIT_TIMEOUT)
            {
                assert(FALSE);
            }
            // このセッションからのイベントはもうない
        }
    }

    // 完全なシャットダウン操作
    if (SUCCEEDED(hr))
    {
        // Media Sourceをシャットダウンする (同期操作、イベントなし)
        if (m_pMediaSource)
        {
            (void)m_pMediaSource->Shutdown();
        }

        // Media Sessionをシャットダウンする (同期操作、イベントなし)
        if (m_pMediaSession)
        {
            (void)m_pMediaSession->Shutdown();
        }
    }

    SafeRelease(&m_pMediaSource);
    SafeRelease(&m_pMediaSession);

    m_playerState = Closed;
    return hr;
}

// このオブジェクトによって保持されているすべてのリソースを解放する
HRESULT CPlayer::Shutdown()
{
    // セッションを閉じる
    HRESULT hr = CloseSession();

    // Media Foundationプラットフォームをシャットダウンする
    MFShutdown();

    if (m_hCloseEvent)
    {
        CloseHandle(m_hCloseEvent);
        m_hCloseEvent = NULL;
    }

    return hr;
}