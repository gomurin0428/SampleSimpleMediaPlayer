#include "MediaPlayer.h"
#include <cassert>
#include <mfapi.h>
#include "mfidl.h"
#include "shlwapi.h"
#include <new>
#include <cassert>
#include "Mfidl.h"
#include "evr.h"
#include "Mfapi.h"

HRESULT easyPlay(HWND hVideo, HWND hEvent) {
	MediaPlayer* player = NULL;
	HRESULT result = MediaPlayer::CreateInstance(hVideo, hEvent, &player);
	result = player->OpenFromURI(L"test.mp4");
	return result;
}

HRESULT MediaPlayer::CreateSession()
{
	//古いセッションを閉じる。
	HRESULT hr = CloseSession();
	if (FAILED(hr)) return hr;

	assert(mediaPlayerState == Closed);

	//MediaSessionを作成する。
	hr = MFCreateMediaSession(NULL, &m_pMediaSession);
	if (FAILED(hr)) return hr;

	//元のソースではこのあたりでBeginGetEventを呼んでいたが、意図がよくわからないのでとりあえず消す。

	//IMFMediaEvent *tmpEvent = NULL;

	//hr = m_pMediaSession->GetEvent(0, &tmpEvent);
	//if (FAILED(hr)) return hr;

	mediaPlayerState = Ready;

	return hr;

}

HRESULT MediaPlayer::CloseSession()
{
	HRESULT hr = S_OK;

	SafeRelease(&m_pVideoDisplayControl);

	if (m_pMediaSession)
	{
		mediaPlayerState = Closing;

		hr = m_pMediaSession->Close();

		if (SUCCEEDED(hr))
		{
			DWORD dwTimeoutMS = 5000;
			Sleep(1000);
			//元のソースではこの辺りで閉じるイベントを待つが、C++でのイベントがよく分からないのでとりあえず1秒待つ。
		}
	}

	if (SUCCEEDED(hr))
	{
		if (m_pMediaSource)
		{
			m_pMediaSource->Shutdown();
		}

		if (m_pMediaSession)
		{
			m_pMediaSession->Shutdown();
		}
	}

	SafeRelease(&m_pMediaSource);
	SafeRelease(&m_pMediaSession);

	mediaPlayerState = Closed;

	return hr;

}

HRESULT MediaPlayer::CreateMediaSource(PCWSTR sourceURL, IMFMediaSource** ppMediaSource)
{
	IMFSourceResolver* pSourceResolver = NULL;
	IUnknown* pMediaSource = NULL;

	MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;

	HRESULT hr = MFCreateSourceResolver(&pSourceResolver);
	if (FAILED(hr)) goto done;

	hr = pSourceResolver->CreateObjectFromURL(
		sourceURL,
		MF_RESOLUTION_MEDIASOURCE,
		NULL,
		&ObjectType,
		&pMediaSource
	);
	if (FAILED(hr)) goto done;

	hr = pMediaSource->QueryInterface(IID_PPV_ARGS(ppMediaSource));


done:
	SafeRelease(&pSourceResolver);
	SafeRelease(&pMediaSource);
	return hr;
}

HRESULT MediaPlayer::CreatePlaybackTopology(
	IMFMediaSource* pMediaSource,
	IMFPresentationDescriptor* pPD,
	HWND hVideoWnd,
	IMFTopology** ppTopology
) {
	IMFTopology* pTopology = NULL;
	DWORD dwSourceStreamCount = 0;

	HRESULT hr = MFCreateTopology(&pTopology);
	if (FAILED(hr)) goto done;

	hr = pPD->GetStreamDescriptorCount(&dwSourceStreamCount);
	if (FAILED(hr)) goto done;

	for (DWORD i = 0; i < dwSourceStreamCount; i++)
	{
		hr = AddBranchToPartialTopology(pTopology, pMediaSource, pPD, i, hVideoWnd);
		if (FAILED(hr)) goto done;
	}

	*ppTopology = pTopology;
	(*ppTopology)->AddRef();

done:
	SafeRelease(&pTopology);
	return hr;
}

HRESULT MediaPlayer::AddBranchToPartialTopology(
	IMFTopology *pTopology,
	IMFMediaSource *pMediaSource,
	IMFPresentationDescriptor *pPD,
	DWORD dwStreamIndex,
	HWND hVideoWnd
)
{
	IMFStreamDescriptor *pSD = NULL;
	IMFActivate* pSinkActivate = NULL;
	IMFTopologyNode *pSourceNode = NULL;
	IMFTopologyNode *pOutputNode = NULL;

	BOOL fSelected = false;

	HRESULT hr = pPD->GetStreamDescriptorByIndex(dwStreamIndex, &fSelected, &pSD);
	if (FAILED(hr)) goto done;

	if (fSelected)
	{
		hr = CreateMediaSinkActivate(pSD, hVideoWnd, &pSinkActivate);
		if (FAILED(hr)) goto done;

		hr = AddSourceNode(pTopology, pMediaSource, pPD, pSD, &pSourceNode);
		if (FAILED(hr)) goto done;

		hr = AddOutputNode(pTopology, pSinkActivate, 0, &pOutputNode);
		if (FAILED(hr)) goto done;

		hr = pSourceNode->ConnectOutput(0, pOutputNode, 0);
	}

done:
	SafeRelease(&pSD);
	SafeRelease(&pSinkActivate);
	SafeRelease(&pSourceNode);
	SafeRelease(&pOutputNode);
	return hr;
}

HRESULT MediaPlayer::CreateMediaSinkActivate(
	IMFStreamDescriptor* pSourceSD,
	HWND hVideoClippingWindow,
	IMFActivate** ppActivate
) {
	IMFMediaTypeHandler* pMediaTypeHandler = NULL;
	IMFActivate* pActivate = NULL;

	HRESULT hr = pSourceSD->GetMediaTypeHandler(&pMediaTypeHandler);
	if (FAILED(hr)) goto done;

	GUID guidMajorType;
	hr = pMediaTypeHandler->GetMajorType(&guidMajorType);
	if (FAILED(hr)) goto done;

	if (MFMediaType_Audio == guidMajorType) {
		hr = MFCreateAudioRendererActivate(&pActivate);
	}
	else if (MFMediaType_Video == guidMajorType) 
	{
		hr = MFCreateVideoRendererActivate(hVideoClippingWindow, &pActivate);
	}
	else
	{
		hr = E_FAIL;
	}
	if (FAILED(hr)) goto done;

	*ppActivate = pActivate;
	(*ppActivate)->AddRef();

done:
	SafeRelease(&pMediaTypeHandler);
	SafeRelease(&pActivate);
	return hr;

}

HRESULT	MediaPlayer::AddSourceNode(
	IMFTopology* pTopoloty,
	IMFMediaSource* pMediaSource,
	IMFPresentationDescriptor* pPD,
	IMFStreamDescriptor* pSD,
	IMFTopologyNode** ppSourceNode
) {
	IMFTopologyNode* pNode = NULL;

	HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &pNode);
	if (FAILED(hr)) goto done;

	hr = pNode->SetUnknown(MF_TOPONODE_SOURCE, pMediaSource);
	if (FAILED(hr)) goto done;

	hr = pNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pPD);
	if (FAILED(hr)) goto done;

	hr = pNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, pSD);
	if (FAILED(hr)) goto done;

	hr = pTopoloty->AddNode(pNode);
	if (FAILED(hr)) goto done;

	*ppSourceNode = pNode;
	(*ppSourceNode)->AddRef();

done:
	SafeRelease(&pNode);
	return hr;
}

HRESULT MediaPlayer::AddOutputNode(
	IMFTopology* pTopology,
	IMFActivate* pMediaSinkActivate,
	DWORD dwStreamSinkId,
	IMFTopologyNode** ppOutputNode
) {
	IMFTopologyNode* pNode = NULL;

	HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pNode);
	if (FAILED(hr)) goto done;

	hr = pNode->SetObject(pMediaSinkActivate);
	if (FAILED(hr)) goto done;

	hr = pNode->SetUINT32(MF_TOPONODE_STREAMID, dwStreamSinkId);
	if (FAILED(hr)) goto done;

	hr = pNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, false);
	if (FAILED(hr)) goto done;

	hr = pTopology->AddNode(pNode);
	if (FAILED(hr)) goto done;

	*ppOutputNode = pNode;
	(*ppOutputNode)->AddRef();

done:
	SafeRelease(&pNode);
	return hr;
}

// 再生用のURLを開く
HRESULT MediaPlayer::OpenFromURI(const WCHAR* sURL)
{
	// 1. 新しいMedia Sessionを作成する
	// 2. Media Sourceを作成する
	// 3. Topologyを作成する
	// 4. Topologyをキューに登録する [非同期]
	// 5. 再生を開始する [非同期 - このメソッドでは起こらない]
	//  (MESessionTopologyStatusイベントをHandleEvent()で捕捉し、OnTopologyStatus()からStartPlayback()が呼ばれることで再生が開始される)

	IMFPresentationDescriptor* pSourcePD = NULL;
	IMFTopology* pTopology = NULL;

	PROPVARIANT varStart;

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

	mediaPlayerState = OpenPending;

	// SetTopologyが成功したならば、Media SessionはMESessionTopologySetイベントをキューに登録する

	//テスト用

	Sleep(1000);

	assert(m_pMediaSession != NULL);

	PropVariantInit(&varStart);

	hr = m_pMediaSession->Start(&GUID_NULL, &varStart);
	if (SUCCEEDED(hr))
	{
		// 注：開始は非同期操作。しかしながら、すでに開始していると見なせる。
		// もし開始が後で失敗するならば、エラーコードと共にMESessionStartedイベントを受け取るため、
		// そのとき状態を更新する。
		mediaPlayerState = Started;
	}

	PropVariantClear(&varStart);
	return hr;



done:
	if (FAILED(hr))
	{
		mediaPlayerState = Closed;
	}

	SafeRelease(&pSourcePD);
	SafeRelease(&pTopology);
	return hr;
}

// CPlayerオブジェクトを作成する、静的クラスメソッド
HRESULT MediaPlayer::CreateInstance(
	HWND hWndVideo,
	HWND hWndEvent,     // 通知を受け取るウィンドウ
	MediaPlayer** ppPlayer) // CPlayerオブジェクトへのポインタを受け取るためのポインタ
{
	if (ppPlayer == NULL)
	{
		return E_POINTER;
	}

	// CPlayerオブジェクトを作成する
	MediaPlayer *pPlayer = new (std::nothrow) MediaPlayer(hWndVideo, hWndEvent);
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
		//pPlayer->ShutDown();
	}
	return hr;
}

MediaPlayer::MediaPlayer(HWND hWndVideo, HWND hWndEvent) :
	m_pMediaSession(NULL),
	m_pMediaSource(NULL),
	m_pVideoDisplayControl(NULL),
	m_hWndVideo(hWndVideo),
	m_hWndEvent(hWndEvent),
	mediaPlayerState(Closed),
	m_hCloseEvent(NULL),
	m_nRefCount(1)
{
}

MediaPlayer::~MediaPlayer()
{
	assert(m_pMediaSession == NULL);
	// FALSEならば、アプリケーションはShutdown()を呼ばなかった。

	// CPlayerがMedia SessionでIMediaEventGenerator::BeginGetEventを呼ぶとき、
	// Media SessionはCPlayerの参照カウントを保持する

	// これはCPlayerとMedia Sessionの間の循環参照カウントを作成する。
	// Shutdownの呼び出しは循環参照カウントを破壊する。

	// もしCreateInstanceが失敗ならば、アプリケーションはShutdownを呼ばない。
	// その場合は、デストラクタでShutdownを呼ぶ。

	ShutDown();
}
HRESULT MediaPlayer::Initialize()
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

// このオブジェクトによって保持されているすべてのリソースを解放する
HRESULT MediaPlayer::ShutDown()
{
	//// セッションを閉じる
	//HRESULT hr = CloseSession();

	// Media Foundationプラットフォームをシャットダウンする
	MFShutdown();

	//if (m_hCloseEvent)
	//{
	//	CloseHandle(m_hCloseEvent);
	//	m_hCloseEvent = NULL;
	//}

	return 1;
}