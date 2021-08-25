#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <evr.h>
#include "shlwapi.h"

    const UINT WM_APP_PLAYER_EVENT = WM_APP + 1;

    // CPlayer�I�u�W�F�N�g���Ȃ蓾���Ԃ̒�`
    enum PlayerState
    {
        Closed = 0,  // �Z�b�V�����Ȃ�
        Ready,       // �Z�b�V�����͍쐬����A�t�@�C�����J���������ł��Ă���
        OpenPending, // �Z�b�V�����̓t�@�C�����J���Ă���
        Started,     // �Z�b�V�����̓t�@�C�����Đ����Ă���
        Paused,      // �Z�b�V�����͈ꎞ��~���Ă���
        Stopped,     // �Z�b�V�����͒�~���Ă��� (�Đ��̏����͂ł��Ă���)
        Closing      // �A�v���P�[�V�����̓Z�b�V��������Ă��邪�AMESessionClosed��҂��Ă���
    };

    class CPlayer : public IMFAsyncCallback
    {
    public:
        __declspec(dllexport) static HRESULT CreateInstance(HWND hVideo, HWND hEvent, CPlayer** ppPlayer);

        template <class T> static void SafeRelease(T** ppT)
        {
            if (*ppT)
            {
                (*ppT)->Release();
                *ppT = NULL;
            }
        }

        // IUnknown ���\�b�h
        // �I�u�W�F�N�g�̐������Ԃ��Q�ƃJ�E���g (m_nRefCount) �Ő��䂷��
        STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
        STDMETHODIMP_(ULONG) AddRef();
        STDMETHODIMP_(ULONG) Release();

        // IMFAsyncCallback ���\�b�h
        STDMETHODIMP GetParameters(DWORD*, DWORD*)
        {
            // ���̃��\�b�h�̎����͔C��
            return E_NOTIMPL;
        }
        STDMETHODIMP Invoke(IMFAsyncResult* pAsyncResult);

        // �Đ�
        HRESULT OpenURL(const WCHAR* sURL);
        HRESULT Play();
        HRESULT Pause();
        HRESULT Stop();
        HRESULT Shutdown();
        HRESULT HandleEvent(UINT_PTR pUnkPtr);
        PlayerState GetState() const
        {
            return m_playerState;
        }

        // �f���@�\
        HRESULT Repaint();
        HRESULT ResizeVideo(WORD width, WORD height);

        BOOL HasVideo() const
        {
            return (m_pVideoDisplayControl != NULL);
        }

    protected:

        // �R���X�g���N�^�̓v���C�x�[�g�B�C���X�^���X���ɂ́A�ÓI��CreateInstance���\�b�h��p����B
        CPlayer(HWND hVideo, HWND hEvent);

        // �f�X�g���N�^�̓v���C�x�[�g�B�Ăяo�����́ARelease���\�b�h���ĂԁB
        virtual ~CPlayer();

        HRESULT Initialize();
        HRESULT CreateSession();
        HRESULT CloseSession();
        HRESULT StartPlayback();

        // Media �C�x���g�n���h��
        virtual HRESULT OnTopologyStatus(IMFMediaEvent* pMediaEvent);
        virtual HRESULT OnPresentationEnded(IMFMediaEvent* pMediaEvent);
        virtual HRESULT OnNewPresentation(IMFMediaEvent* pMediaEvent);

        // �ǉ��̃Z�b�V�����C�x���g��ߑ����邽�߂̃I�[�o�[���C�h
        virtual HRESULT OnSessionEvent(IMFMediaEvent*, MediaEventType)
        {
            return S_OK;
        }

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
        PlayerState m_playerState; // Media Session�̌��݂̏��
        HANDLE m_hCloseEvent;      // �I�����ɑҋ@����C�x���g
    };
