#pragma once
#include <wtypes.h>
#include <mfidl.h>
#include <evr.h>

extern "C" {
	__declspec(dllexport)  HRESULT easyPlay(HWND hVideo, HWND hEvent);
}

enum MediaPlayerState
{
	Closed = 0,  // �Z�b�V�����Ȃ�
	Ready,       // �Z�b�V�����͍쐬����A�t�@�C�����J���������ł��Ă���
	OpenPending, // �Z�b�V�����̓t�@�C�����J���Ă���
	Started,     // �Z�b�V�����̓t�@�C�����Đ����Ă���
	Paused,      // �Z�b�V�����͈ꎞ��~���Ă���
	Stopped,     // �Z�b�V�����͒�~���Ă��� (�Đ��̏����͂ł��Ă���)
	Closing      // �A�v���P�[�V�����̓Z�b�V��������Ă��邪�AMESessionClosed��҂��Ă���
};

class MediaPlayer {
public:

	//�R���X�g���N�^�͌��J���Ȃ��B���̃��\�b�h�ŃC���X�^���X���쐬����B
	static HRESULT CreateInstance(HWND hVideo, HWND hEvent, MediaPlayer** ppPlayer);

	template <class T> static void SafeRelease(T** ppT)
	{
		if (*ppT)
		{
			(*ppT)->Release();
			*ppT = NULL;
		}
	}

	//URI���w�肵�ă��f�B�A���J��
	HRESULT OpenFromURI(const WCHAR* sURL);

	//�Đ����s���B���f�B�A���J���ꂽ��ԂŎ��s����B
	HRESULT Play();

	//�ꎞ��~���s���B���f�B�A���J���ꂽ��ԂŎ��s����B
	HRESULT Pause();

	//���̃N���X�̎��w�莞�ԕ����f�B�A���ɐi�߂�B��ɐi�߂Ȃ�������i�߂�Ƃ���܂Ői�ށB
	HRESULT Next();

	//���̃N���X�̎��w�莞�ԕ����f�B�A�����ɐi�߂�B���ɐi�߂Ȃ�������i�߂�Ƃ���܂Ői�ށB
	HRESULT Back();

	//���̃I�u�W�F�N�g�̎����\�[�X��S�Ċ��S�ɊJ������B
	HRESULT ShutDown();

	//���ݍĐ����Ă��郁�f�B�A�̍Đ��ʒu��ύX����B�ύX������͈ꎞ��~������ԂƂȂ�B
	HRESULT ChangeCurrentPosition();

	//�ꎞ��~���Ă���ꍇ�ɂ��̃��\�b�h���Ăяo���ƁA���݂̍Đ��ʒu�̃T���l�C����\������B
	HRESULT ReDraw();

	//�J����Ă��郁�f�B�A���w�肵���T�C�Y�ɕύX����B
	HRESULT ResizeVideo(WORD width, WORD height);

	//�J�������f�B�A������������Ă��邩��Ԃ��B
	BOOL HasVideo();

	//�J�������f�B�A�������������Ă��邩��Ԃ��B
	BOOL HasAudio();

protected:

	MediaPlayer(HWND hVideo, HWND hEvent);

	virtual ~MediaPlayer();

	HRESULT Initialize();

	HRESULT CreateSession();

	HRESULT CloseSession();

private:

	HRESULT CreateMediaSource(PCWSTR pszURL, IMFMediaSource** ppMediaSource);

	HRESULT CreatePlaybackTopology(IMFMediaSource*, IMFPresentationDescriptor*, HWND, IMFTopology**);

	HRESULT AddBranchToPartialTopology(IMFTopology*, IMFMediaSource*, IMFPresentationDescriptor*, DWORD, HWND);

	HRESULT CreateMediaSinkActivate(IMFStreamDescriptor*, HWND, IMFActivate**);

	HRESULT AddSourceNode(IMFTopology*, IMFMediaSource*, IMFPresentationDescriptor*, IMFStreamDescriptor*, IMFTopologyNode**);

	HRESULT AddOutputNode(IMFTopology*, IMFActivate*, DWORD, IMFTopologyNode**);

protected:
	long m_nRefCount; // �Q�ƃJ�E���g

	IMFMediaSession* m_pMediaSession;
	IMFMediaSource* m_pMediaSource;
	IMFVideoDisplayControl* m_pVideoDisplayControl;

	HWND m_hWndVideo;          // �f���E�B���h�E
	HWND m_hWndEvent;          // �C�x���g���󂯎�邽�߂̃A�v���P�[�V���� �E�B���h�E
	MediaPlayerState mediaPlayerState; // Media Session�̌��݂̏��
	HANDLE m_hCloseEvent;      // �I�����ɑҋ@����C�x���g


};