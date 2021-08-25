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

// IUnknown ���\�b�h
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

// CPlayer�I�u�W�F�N�g���쐬����A�ÓI�N���X���\�b�h
HRESULT CPlayer::CreateInstance(
    HWND hWndVideo,
    HWND hWndEvent,     // �ʒm���󂯎��E�B���h�E
    CPlayer** ppPlayer) // CPlayer�I�u�W�F�N�g�ւ̃|�C���^���󂯎�邽�߂̃|�C���^
{
    if (ppPlayer == NULL)
    {
        return E_POINTER;
    }

    // CPlayer�I�u�W�F�N�g���쐬����
    CPlayer* pPlayer = new (std::nothrow) CPlayer(hWndVideo, hWndEvent);
    if (pPlayer == NULL)
    {
        return E_OUTOFMEMORY;
    }

    // ����������
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
    // Media Foundation�v���b�g�t�H�[���̊J�n
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
    // FALSE�Ȃ�΁A�A�v���P�[�V������Shutdown()���Ă΂Ȃ������B

    // CPlayer��Media Session��IMediaEventGenerator::BeginGetEvent���ĂԂƂ��A
    // Media Session��CPlayer�̎Q�ƃJ�E���g��ێ�����

    // �����CPlayer��Media Session�̊Ԃ̏z�Q�ƃJ�E���g���쐬����B
    // Shutdown�̌Ăяo���͏z�Q�ƃJ�E���g��j�󂷂�B

    // ����CreateInstance�����s�Ȃ�΁A�A�v���P�[�V������Shutdown���Ă΂Ȃ��B
    // ���̏ꍇ�́A�f�X�g���N�^��Shutdown���ĂԁB

    Shutdown();
}

// �Đ��p��URL���J��
HRESULT CPlayer::OpenURL(const WCHAR* sURL)
{
    // 1. �V����Media Session���쐬����
    // 2. Media Source���쐬����
    // 3. Topology���쐬����
    // 4. Topology���L���[�ɓo�^���� [�񓯊�]
    // 5. �Đ����J�n���� [�񓯊� - ���̃��\�b�h�ł͋N����Ȃ�]
    //  (MESessionTopologyStatus�C�x���g��HandleEvent()�ŕߑ����AOnTopologyStatus()����StartPlayback()���Ă΂�邱�ƂōĐ����J�n�����)

    IMFPresentationDescriptor* pSourcePD = NULL;
    IMFTopology* pTopology = NULL;

    // 1. Media Session���쐬����
    HRESULT hr = CreateSession();
    if (FAILED(hr)) goto done;

    // 2. Media Source���쐬����
    hr = CreateMediaSource(sURL, &m_pMediaSource);
    if (FAILED(hr)) goto done;

    // Media Source�p�̃v���[���e�[�V�����L�q�q (presentation descriptor) ���쐬����
    hr = m_pMediaSource->CreatePresentationDescriptor(&pSourcePD);
    if (FAILED(hr)) goto done;

    // 3. ����Topology (partial topology) ���쐬����
    hr = CreatePlaybackTopology(m_pMediaSource, pSourcePD, m_hWndVideo, &pTopology);
    if (FAILED(hr)) goto done;

    // 4. Media Session��Topology��ݒ肷��
    hr = m_pMediaSession->SetTopology(0, pTopology);
    if (FAILED(hr)) goto done;

    m_playerState = OpenPending;

    // SetTopology�����������Ȃ�΁AMedia Session��MESessionTopologySet�C�x���g���L���[�ɓo�^����

done:
    if (FAILED(hr))
    {
        m_playerState = Closed;
    }

    SafeRelease(&pSourcePD);
    SafeRelease(&pTopology);
    return hr;
}

// URL����Media Source���쐬����
HRESULT CPlayer::CreateMediaSource(PCWSTR sourceURL, IMFMediaSource** ppMediaSource)
{
    IMFSourceResolver* pSourceResolver = NULL;
    IUnknown* pMediaSource = NULL;

    // Source Resolver���쐬����
    HRESULT hr = MFCreateSourceResolver(&pSourceResolver);

    MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;
    if (FAILED(hr)) goto done;

    // Source Resolver���g�p���āAMedia Source���쐬����

    // ���F�ȒP�̂��߂ɁA���̃T���v���ł�Media Source�̍쐬�ɓ������\�b�h��p���Ă���B
    // �������Ȃ���Media Source�̍쐬�́A��������̎��Ԃ�v����B���Ƀl�b�g���[�N �\�[�X�̏ꍇ�́B
    // ��艞�����̗ǂ�UI�̂��߂ɂ́A�񓯊���BeginCreateObjectFromURL���\�b�h��p����B

    hr = pSourceResolver->CreateObjectFromURL(
        sourceURL,
        MF_RESOLUTION_MEDIASOURCE, // �\�[�X �I�u�W�F�N�g�̍쐬
        NULL,         // �ȗ��\�ȃv���p�e�B �X�g�A
        &ObjectType,  // �쐬���ꂽ�I�u�W�F�N�g�^���󂯎��|�C���^
        &pMediaSource // Media Source�ւ̃|�C���^���󂯎��|�C���^
    );
    if (FAILED(hr)) goto done;

    // Media Source����IMFMediaSource�C���^�[�t�F�C�X���擾����
    hr = pMediaSource->QueryInterface(IID_PPV_ARGS(ppMediaSource));

done:
    SafeRelease(&pSourceResolver);
    SafeRelease(&pMediaSource);
    return hr;
}

// Media Source����Đ�Topology���쐬����
HRESULT CPlayer::CreatePlaybackTopology(
    IMFMediaSource* pMediaSource,
    IMFPresentationDescriptor* pPD,
    HWND hVideoWnd,
    IMFTopology** ppTopology)
{
    IMFTopology* pTopology = NULL;


    // Media Source�ɂ���X�g���[���̐����擾����
    DWORD dwSourceStreamsCount = 0;

    // �V����Topology�̍쐬
    HRESULT hr = MFCreateTopology(&pTopology);
    if (FAILED(hr)) goto done;

    hr = pPD->GetStreamDescriptorCount(&dwSourceStreamsCount);
    if (FAILED(hr)) goto done;

    // �X�g���[�����Ƃ�Topology�m�[�h���쐬���A�����Topology�֒ǉ�����
    for (DWORD i = 0; i < dwSourceStreamsCount; i++)
    {
        hr = AddBranchToPartialTopology(pTopology, pMediaSource, pPD, i, hVideoWnd);
        if (FAILED(hr)) goto done;
    }

    // �Ăяo������IMFTopology�|�C���^��Ԃ�
    *ppTopology = pTopology;
    (*ppTopology)->AddRef();

done:
    SafeRelease(&pTopology);
    return hr;
}

// 1�̃X�g���[���p�ɁATopology�u�����`��ǉ�����
//
// �X�g���[�����ƂɁA���̊֐��͎��̂悤�ɏ�������
//
// 1. �X�g���[���Ɋ֘A����\�[�X �m�[�h���쐬����
// 2. �����_���p�̏o�̓m�[�h���쐬����
// 3. 2�̃m�[�h��ڑ�����
//
// Media Session�͕K�v�ȃf�R�[�_��ǉ�����
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
        // ���f�B�A �V���N �A�N�e�B�u���I�u�W�F�N�g���쐬����
        hr = CreateMediaSinkActivate(pSD, hVideoWnd, &pSinkActivate);
        if (FAILED(hr)) goto done;

        // ���̃X�g���[���ɁA�\�[�X �m�[�h��ǉ�����
        hr = AddSourceNode(pTopology, pMediaSource, pPD, pSD, &pSourceNode);
        if (FAILED(hr)) goto done;

        // �����_���p�́A�o�̓m�[�h���쐬����
        hr = AddOutputNode(pTopology, pSinkActivate, 0, &pOutputNode);
        if (FAILED(hr)) goto done;

        // �\�[�X�m�[�h���o�̓m�[�h�ɐڑ�����
        hr = pSourceNode->ConnectOutput(0, pOutputNode, 0);
    }
    // else: �I������Ă��Ȃ���΁A�u�����`��ǉ����Ȃ�

done:
    SafeRelease(&pSD);
    SafeRelease(&pSinkActivate);
    SafeRelease(&pSourceNode);
    SafeRelease(&pOutputNode);
    return hr;
}

// �X�g���[�� ���f�B�A�^�C�v����b�Ƃ���A�����_���p�̃A�N�e�B�u���I�u�W�F�N�g�쐬����
HRESULT CPlayer::CreateMediaSinkActivate(
    IMFStreamDescriptor* pSourceSD,
    HWND hVideoClippingWindow,
    IMFActivate** ppActivate)
{
    IMFMediaTypeHandler* pMediaTypeHandler = NULL;
    IMFActivate* pActivate = NULL;

    // �X�g���[���p�̃��f�B�A�^�C�v �n���h�����擾����
    HRESULT hr = pSourceSD->GetMediaTypeHandler(&pMediaTypeHandler);
    if (FAILED(hr)) goto done;

    // ���W���[���f�B�A�^�C�v���擾����
    GUID guidMajorType;
    hr = pMediaTypeHandler->GetMajorType(&guidMajorType);
    if (FAILED(hr)) goto done;

    // ���f�B�A�^�C�v����b�Ƃ���A�����_���p��IMFActivate�I�u�W�F�N�g���쐬����
    if (MFMediaType_Audio == guidMajorType)
    {
        // ���������_�����쐬����
        hr = MFCreateAudioRendererActivate(&pActivate);
    }
    else if (MFMediaType_Video == guidMajorType)
    {
        // �f�������_�����쐬����
        hr = MFCreateVideoRendererActivate(hVideoClippingWindow, &pActivate);
    }
    else
    {
        // ���m�̃X�g���[���^�C�v
        hr = E_FAIL;
        // �C�ӂ����A���s�̑���ɂ��̃X�g���[����I�������ł���
    }
    if (FAILED(hr)) goto done;

    // �Ăяo������IMFActivate�|�C���^��Ԃ�
    *ppActivate = pActivate;
    (*ppActivate)->AddRef();

done:
    SafeRelease(&pMediaTypeHandler);
    SafeRelease(&pActivate);
    return hr;
}

// Topology�փ\�[�X�m�[�h��ǉ�����
HRESULT CPlayer::AddSourceNode(
    IMFTopology* pTopology,
    IMFMediaSource* pMediaSource,
    IMFPresentationDescriptor* pPD,
    IMFStreamDescriptor* pSD,
    IMFTopologyNode** ppSourceNode)
{
    IMFTopologyNode* pNode = NULL;

    // �m�[�h�̍쐬
    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &pNode);
    if (FAILED(hr)) goto done;

    // �����̐ݒ�
    hr = pNode->SetUnknown(MF_TOPONODE_SOURCE, pMediaSource);
    if (FAILED(hr)) goto done;

    hr = pNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pPD);
    if (FAILED(hr)) goto done;

    hr = pNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, pSD);
    if (FAILED(hr)) goto done;

    // Topology�փm�[�h��ǉ�����
    hr = pTopology->AddNode(pNode);
    if (FAILED(hr)) goto done;

    // �Ăяo�����փ|�C���^��Ԃ�
    *ppSourceNode = pNode;
    (*ppSourceNode)->AddRef();

done:
    SafeRelease(&pNode);
    return hr;
}

// Topology�֏o�̓m�[�h��ǉ�����
HRESULT CPlayer::AddOutputNode(
    IMFTopology* pTopology,
    IMFActivate* pMediaSinkActivate,
    DWORD dwStreamSinkId,
    IMFTopologyNode** ppOutputNode)
{
    IMFTopologyNode* pNode = NULL;

    // �m�[�h���쐬����
    HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pNode);
    if (FAILED(hr)) goto done;

    // �I�u�W�F�N�g �|�C���^��ݒ肷��
    hr = pNode->SetObject(pMediaSinkActivate);
    if (FAILED(hr)) goto done;

    // �X�g���[�� �V���NID������ݒ肷��
    hr = pNode->SetUINT32(MF_TOPONODE_STREAMID, dwStreamSinkId);
    if (FAILED(hr)) goto done;

    hr = pNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE);
    if (FAILED(hr)) goto done;

    // Topology�փm�[�h��ǉ�����
    hr = pTopology->AddNode(pNode);
    if (FAILED(hr)) goto done;

    // �Ăяo�����փ|�C���^��Ԃ�
    *ppOutputNode = pNode;
    (*ppOutputNode)->AddRef();

done:
    SafeRelease(&pNode);
    return hr;
}

// Media Session�̐V�����C���X�^���X���쐬����
HRESULT CPlayer::CreateSession()
{
    // ��������΁A�Â��Z�b�V���������
    HRESULT hr = CloseSession();
    if (FAILED(hr)) goto done;

    assert(m_playerState == Closed);

    // Media Session���쐬����
    hr = MFCreateMediaSession(NULL, &m_pMediaSession);
    if (FAILED(hr)) goto done;

    //// Media Session����̃C�x���g�̃v�����J�n����
    //// ���̃��\�b�h�͔񓯊��ł���A�Ăяo���͑����ɕԂ����B
    //// ����Session�C�x���g�����������Ƃ��ɁAMedia Session��IMFAsyncCallback::Invoke���Ăяo���B
    //hr = m_pMediaSession->BeginGetEvent((IMFAsyncCallback*)this, NULL);
    //if (FAILED(hr)) goto done;

    m_playerState = Ready;

done:
    return hr;
}

// �񓯊���BeginGetEvent���\�b�h�p�̃R�[���o�b�N
HRESULT CPlayer::Invoke(IMFAsyncResult* pAsyncResult)
{
    // ���̃��\�b�h�̓��[�J�[�X���b�h�ŌĂ΂��B

    MediaEventType mediaEventType = MEUnknown;
    IMFMediaEvent* pMediaEvent = NULL;

    // �C�x���g �L���[����C�x���g���擾����
    HRESULT hr = m_pMediaSession->EndGetEvent(pAsyncResult, &pMediaEvent);
    if (FAILED(hr)) goto done;

    // �C�x���g�̎�ނ��擾����
    hr = pMediaEvent->GetType(&mediaEventType);
    if (FAILED(hr)) goto done;


    if (mediaEventType == MESessionClosed) // �Z�b�V�����͕���ꂽ�B
    {
        // �A�v���P�[�V������ m_hCloseEvent�C�x���g���n���h�������̂�ҋ@���Ă���B
        SetEvent(m_hCloseEvent);
    }
    else
    {
        // ���ׂĂ̑��̃C�x���g�̂��߂ɁA�L���[�̎��̃C�x���g���擾����
        hr = m_pMediaSession->BeginGetEvent(this, NULL);
        if (FAILED(hr)) goto done;
    }

    // �A�v���P�[�V�����̏�Ԃ��m�F����

    // ����IMFMediaSession::Close �̌Ăяo�����������Ȃ�΁A�A�v���P�[�V������
    // m_hCloseEvent �C�x���g��ҋ@���A�A�v���P�[�V�����̃��b�Z�[�W���[�v���u���b�N����Ă���B
    // �����Ȃ��΃v���C�x�[�g �E�B���h�E ���b�Z�[�W���A�v���P�[�V�����֑���B

    if (m_playerState != Closing)
    {
        // �C�x���g�ɎQ�ƃJ�E���g���c��
        pMediaEvent->AddRef();

        // WM_APP_PLAYER_EVENT���b�Z�[�W���A�v���P�[�V�����֑���
        PostMessage(m_hWndEvent, WM_APP_PLAYER_EVENT, (WPARAM)pMediaEvent, (LPARAM)mediaEventType);
    }

done:
    SafeRelease(&pMediaEvent);
    return S_OK;
}

// ���̃��\�b�h�́AWM_APP_PLAYER_EVENT���b�Z�[�W���󂯎�����A�v���P�[�V��������Ăяo�����
HRESULT CPlayer::HandleEvent(UINT_PTR pMediaEventPtr)
{
    IMFMediaEvent* pMediaEvent = (IMFMediaEvent*)pMediaEventPtr;
    if (pMediaEvent == NULL)
    {
        return E_POINTER;
    }

    // �C�x���g�̎�ނ��擾����
    MediaEventType mediaEventType = MEUnknown;
    HRESULT hr = pMediaEvent->GetType(&mediaEventType);

    HRESULT hrStatus = S_OK;
    if (FAILED(hr)) goto done;

    // �C�x���g�̏�Ԃ��擾����
    // �C�x���g�̂��������ƂȂ������삪�������Ȃ������Ȃ�΁A��Ԃ͎��s�R�[�h�ƂȂ�
    hr = pMediaEvent->GetStatus(&hrStatus);

    // �񓯊����삪�����������m�F����
    if (SUCCEEDED(hr) && FAILED(hrStatus))
    {
        hr = hrStatus;
    }
    if (FAILED(hr)) goto done;

    switch (mediaEventType)
    {
    case MESessionTopologyStatus: // Topology�̏�Ԃ��ω�����
        hr = OnTopologyStatus(pMediaEvent);
        break;

    case MEEndOfPresentation: // �v���[���e�[�V�������I������ (�Đ����t�@�C���̍Ō�ɓ��B����)
        hr = OnPresentationEnded(pMediaEvent);
        break;

    case MENewPresentation: // �V�����v���[���e�[�V�������p�ӂł���
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

        // EVR����IMFVideoDisplayControl �C���^�[�t�F�C�X���擾����B
        // ���̌Ăяo���́A���f�B�A�t�@�C���ɉf���X�g���[�����Ȃ��Ƃ��͎��s���邱�Ƃ��\�������B

        //hr = MFGetService(m_pMediaSession, MR_VIDEO_RENDER_SERVICE, IID_PPV_ARGS(&m_pVideoDisplayControl));

        hr = StartPlayback();
    }
    return hr;
}

// MEEndOfPresentation�C�x���g�p�̃n���h��
HRESULT CPlayer::OnPresentationEnded(IMFMediaEvent* pMediaEvent)
{
    // �Z�b�V�����͎����I�Ɏ��g���A��~��Ԃɂ���
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

// MENewPresentation�p�̃n���h��
//
// ���̃C�x���g�́AMedia Source�ɐV����Topology��v������V�����v���[���e�[�V����������Ȃ�Α�����
HRESULT CPlayer::OnNewPresentation(IMFMediaEvent* pMediaEvent)
{
    IMFPresentationDescriptor* pPD = NULL;
    IMFTopology* pTopology = NULL;

    // �C�x���g����v���[���e�[�V�����L�q�q���擾����
    HRESULT hr = GetEventObject(pMediaEvent, &pPD);
    if (FAILED(hr)) goto done;

    // ����Topology���쐬����
    hr = CreatePlaybackTopology(m_pMediaSource, pPD, m_hWndVideo, &pTopology);
    if (FAILED(hr)) goto done;

    // Media Session�ɁATopology��ݒ肷��
    hr = m_pMediaSession->SetTopology(0, pTopology);
    if (FAILED(hr)) goto done;

    m_playerState = OpenPending;

done:
    SafeRelease(&pTopology);
    SafeRelease(&pPD);
    return S_OK;
}



// ���݈ʒu����A�Đ����J�n����
HRESULT CPlayer::StartPlayback()
{
    assert(m_pMediaSession != NULL);

    PROPVARIANT varStart;
    PropVariantInit(&varStart);

    HRESULT hr = m_pMediaSession->Start(&GUID_NULL, &varStart);
    if (SUCCEEDED(hr))
    {
        // ���F�J�n�͔񓯊�����B�������Ȃ���A���łɊJ�n���Ă���ƌ��Ȃ���B
        // �����J�n����Ŏ��s����Ȃ�΁A�G���[�R�[�h�Ƌ���MESessionStarted�C�x���g���󂯎�邽�߁A
        // ���̂Ƃ���Ԃ��X�V����B
        m_playerState = Started;
    }

    PropVariantClear(&varStart);
    return hr;
}

// �ꎞ��~�܂��͒�~����A�Đ����J�n����
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

// �Đ��̈ꎞ��~
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

// �Đ��̒�~
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

// �f���E�B���h�E�̍ĕ`�� (���̃��\�b�h��WM_PAINT�ŌĂ�)
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
//// WM_PAINT ���b�Z�[�W�̃n���h��
//void OnPaint(HWND hwnd)
//{
//    PAINTSTRUCT ps;
//    HDC hdc = BeginPaint(hwnd, &ps);
//
//    if (g_pPlayer && g_pPlayer->HasVideo())
//    {
//        // ����͍Đ����B�v���[���ɍĕ`������߂�
//        g_pPlayer->Repaint();
//    }
//    else
//    {
//        // ���悪�Đ�����Ă��Ȃ����߁A�A�v���P�[�V���� �E�B���h�E��`�悵�Ȃ���΂Ȃ�Ȃ�
//        RECT rc;
//        GetClientRect(hwnd, &rc);
//        FillRect(hdc, &rc, (HBRUSH)COLOR_WINDOW);
//    }
//    EndPaint(hwnd, &ps);
//}

// �f���̋�`�̈�̃T�C�Y�ύX
// (�f���E�B���h�E�̃T�C�Y���ύX���ꂽ�Ȃ�΁A���̃��\�b�h���Ă�)
HRESULT CPlayer::ResizeVideo(WORD width, WORD height)
{
    if (m_pVideoDisplayControl)
    {
        // �ړI�̋�`��ݒ肷��B�f���Ɠ��T�C�Y�Ƃ���Ȃ�� (0,0,1,1) �Ɛݒ肷��B
        RECT rcDest = { 0, 0, width, height };
        return m_pVideoDisplayControl->SetVideoPosition(NULL, &rcDest);
    }
    else
    {
        return S_OK;
    }
}

// Media Session�����
HRESULT CPlayer::CloseSession()
{
    // IMFMediaSession::Close ���\�b�h�͔񓯊������A
    // CPlayer::CloseSession ���\�b�h��MESessionClosed �C�x���g��ҋ@����B
    //
    // MESessionClosed ��Media Session�������N�������Ō�̃C�x���g�ł��邱�Ƃ��ۏ؂����B

    HRESULT hr = S_OK;

    SafeRelease(&m_pVideoDisplayControl);

    // �ŏ���Media Session�����
    if (m_pMediaSession)
    {
        m_playerState = Closing;

        hr = m_pMediaSession->Close();
        if (SUCCEEDED(hr))
        {
            // ���鑀��̊�����҂�
            DWORD dwTimeoutMS = 5000;
            DWORD dwWaitResult = WaitForSingleObject(m_hCloseEvent, dwTimeoutMS);
            if (dwWaitResult == WAIT_TIMEOUT)
            {
                assert(FALSE);
            }
            // ���̃Z�b�V��������̃C�x���g�͂����Ȃ�
        }
    }

    // ���S�ȃV���b�g�_�E������
    if (SUCCEEDED(hr))
    {
        // Media Source���V���b�g�_�E������ (��������A�C�x���g�Ȃ�)
        if (m_pMediaSource)
        {
            (void)m_pMediaSource->Shutdown();
        }

        // Media Session���V���b�g�_�E������ (��������A�C�x���g�Ȃ�)
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

// ���̃I�u�W�F�N�g�ɂ���ĕێ�����Ă��邷�ׂẴ��\�[�X���������
HRESULT CPlayer::Shutdown()
{
    // �Z�b�V���������
    HRESULT hr = CloseSession();

    // Media Foundation�v���b�g�t�H�[�����V���b�g�_�E������
    MFShutdown();

    if (m_hCloseEvent)
    {
        CloseHandle(m_hCloseEvent);
        m_hCloseEvent = NULL;
    }

    return hr;
}