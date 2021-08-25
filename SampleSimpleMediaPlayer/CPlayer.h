#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <evr.h>
#include "shlwapi.h"

extern "C" {
    __declspec(dllexport)  int easyPlay(HWND hVideo, HWND hEvent);
}

    const UINT WM_APP_PLAYER_EVENT = WM_APP + 1;

    // CPlayerオブジェクトがなり得る状態の定義
    enum PlayerState
    {
        Closed = 0,  // セッションなし
        Ready,       // セッションは作成され、ファイルを開く準備ができている
        OpenPending, // セッションはファイルを開いている
        Started,     // セッションはファイルを再生している
        Paused,      // セッションは一時停止している
        Stopped,     // セッションは停止している (再生の準備はできている)
        Closing      // アプリケーションはセッションを閉じているが、MESessionClosedを待っている
    };

    class CPlayer : public IMFAsyncCallback
    {
    public:
        static HRESULT CreateInstance(HWND hVideo, HWND hEvent, CPlayer** ppPlayer);

        template <class T> static void SafeRelease(T** ppT)
        {
            if (*ppT)
            {
                (*ppT)->Release();
                *ppT = NULL;
            }
        }

        // IUnknown メソッド
        // オブジェクトの生存期間を参照カウント (m_nRefCount) で制御する
        STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
        STDMETHODIMP_(ULONG) AddRef();
        STDMETHODIMP_(ULONG) Release();

        // IMFAsyncCallback メソッド
        STDMETHODIMP GetParameters(DWORD*, DWORD*)
        {
            // このメソッドの実装は任意
            return E_NOTIMPL;
        }
        STDMETHODIMP Invoke(IMFAsyncResult* pAsyncResult);

        // 再生
        HRESULT OpenURL(const WCHAR* sURL);
        HRESULT Play();
        HRESULT Pause();
        HRESULT Stop();
        HRESULT Shutdown();

        HRESULT StartPlayback();
        HRESULT HandleEvent(UINT_PTR pUnkPtr);
        PlayerState GetState() const
        {
            return m_playerState;
        }

        // 映像機能
        HRESULT Repaint();
        HRESULT ResizeVideo(WORD width, WORD height);

        BOOL HasVideo() const
        {
            return (m_pVideoDisplayControl != NULL);
        }

    protected:

        // コンストラクタはプライベート。インスタンス化には、静的なCreateInstanceメソッドを用いる。
        CPlayer(HWND hVideo, HWND hEvent);

        // デストラクタはプライベート。呼び出し元は、Releaseメソッドを呼ぶ。
        virtual ~CPlayer();

        HRESULT Initialize();
        HRESULT CreateSession();
        HRESULT CloseSession();

        // Media イベントハンドラ
        virtual HRESULT OnTopologyStatus(IMFMediaEvent* pMediaEvent);
        virtual HRESULT OnPresentationEnded(IMFMediaEvent* pMediaEvent);
        virtual HRESULT OnNewPresentation(IMFMediaEvent* pMediaEvent);

        // 追加のセッションイベントを捕捉するためのオーバーライド
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
        long m_nRefCount; // 参照カウント

        IMFMediaSession* m_pMediaSession;
        IMFMediaSource* m_pMediaSource;
        IMFVideoDisplayControl* m_pVideoDisplayControl;

        HWND m_hWndVideo;          // 映像ウィンドウ
        HWND m_hWndEvent;          // イベントを受け取るためのアプリケーション ウィンドウ
        PlayerState m_playerState; // Media Sessionの現在の状態
        HANDLE m_hCloseEvent;      // 終了中に待機するイベント
    };
