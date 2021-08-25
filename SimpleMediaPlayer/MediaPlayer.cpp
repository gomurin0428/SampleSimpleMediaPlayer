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
	//�Â��Z�b�V���������B
	HRESULT hr = CloseSession();
	if (FAILED(hr)) return hr;

	assert(mediaPlayerState == Closed);

	//MediaSession���쐬����B
	hr = MFCreateMediaSession(NULL, &m_pMediaSession);
	if (FAILED(hr)) return hr;

	//���̃\�[�X�ł͂��̂������BeginGetEvent���Ă�ł������A�Ӑ}���悭�킩��Ȃ��̂łƂ肠���������B

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
			//���̃\�[�X�ł͂��̕ӂ�ŕ���C�x���g��҂��AC++�ł̃C�x���g���悭������Ȃ��̂łƂ肠����1�b�҂B
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

// �Đ��p��URL���J��
HRESULT MediaPlayer::OpenFromURI(const WCHAR* sURL)
{
	// 1. �V����Media Session���쐬����
	// 2. Media Source���쐬����
	// 3. Topology���쐬����
	// 4. Topology���L���[�ɓo�^���� [�񓯊�]
	// 5. �Đ����J�n���� [�񓯊� - ���̃��\�b�h�ł͋N����Ȃ�]
	//  (MESessionTopologyStatus�C�x���g��HandleEvent()�ŕߑ����AOnTopologyStatus()����StartPlayback()���Ă΂�邱�ƂōĐ����J�n�����)

	IMFPresentationDescriptor* pSourcePD = NULL;
	IMFTopology* pTopology = NULL;

	PROPVARIANT varStart;

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

	mediaPlayerState = OpenPending;

	// SetTopology�����������Ȃ�΁AMedia Session��MESessionTopologySet�C�x���g���L���[�ɓo�^����

	//�e�X�g�p

	Sleep(1000);

	assert(m_pMediaSession != NULL);

	PropVariantInit(&varStart);

	hr = m_pMediaSession->Start(&GUID_NULL, &varStart);
	if (SUCCEEDED(hr))
	{
		// ���F�J�n�͔񓯊�����B�������Ȃ���A���łɊJ�n���Ă���ƌ��Ȃ���B
		// �����J�n����Ŏ��s����Ȃ�΁A�G���[�R�[�h�Ƌ���MESessionStarted�C�x���g���󂯎�邽�߁A
		// ���̂Ƃ���Ԃ��X�V����B
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

// CPlayer�I�u�W�F�N�g���쐬����A�ÓI�N���X���\�b�h
HRESULT MediaPlayer::CreateInstance(
	HWND hWndVideo,
	HWND hWndEvent,     // �ʒm���󂯎��E�B���h�E
	MediaPlayer** ppPlayer) // CPlayer�I�u�W�F�N�g�ւ̃|�C���^���󂯎�邽�߂̃|�C���^
{
	if (ppPlayer == NULL)
	{
		return E_POINTER;
	}

	// CPlayer�I�u�W�F�N�g���쐬����
	MediaPlayer *pPlayer = new (std::nothrow) MediaPlayer(hWndVideo, hWndEvent);
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
	// FALSE�Ȃ�΁A�A�v���P�[�V������Shutdown()���Ă΂Ȃ������B

	// CPlayer��Media Session��IMediaEventGenerator::BeginGetEvent���ĂԂƂ��A
	// Media Session��CPlayer�̎Q�ƃJ�E���g��ێ�����

	// �����CPlayer��Media Session�̊Ԃ̏z�Q�ƃJ�E���g���쐬����B
	// Shutdown�̌Ăяo���͏z�Q�ƃJ�E���g��j�󂷂�B

	// ����CreateInstance�����s�Ȃ�΁A�A�v���P�[�V������Shutdown���Ă΂Ȃ��B
	// ���̏ꍇ�́A�f�X�g���N�^��Shutdown���ĂԁB

	ShutDown();
}
HRESULT MediaPlayer::Initialize()
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

// ���̃I�u�W�F�N�g�ɂ���ĕێ�����Ă��邷�ׂẴ��\�[�X���������
HRESULT MediaPlayer::ShutDown()
{
	//// �Z�b�V���������
	//HRESULT hr = CloseSession();

	// Media Foundation�v���b�g�t�H�[�����V���b�g�_�E������
	MFShutdown();

	//if (m_hCloseEvent)
	//{
	//	CloseHandle(m_hCloseEvent);
	//	m_hCloseEvent = NULL;
	//}

	return 1;
}