#pragma once
#include <wtypes.h>
#include <mfidl.h>
#include <evr.h>

extern "C" {
	__declspec(dllexport)  HRESULT easyPlay(HWND hVideo, HWND hEvent);
}

enum MediaPlayerState
{
	Closed = 0,  // セッションなし
	Ready,       // セッションは作成され、ファイルを開く準備ができている
	OpenPending, // セッションはファイルを開いている
	Started,     // セッションはファイルを再生している
	Paused,      // セッションは一時停止している
	Stopped,     // セッションは停止している (再生の準備はできている)
	Closing      // アプリケーションはセッションを閉じているが、MESessionClosedを待っている
};

class MediaPlayer {
public:

	//コンストラクタは公開しない。このメソッドでインスタンスを作成する。
	static HRESULT CreateInstance(HWND hVideo, HWND hEvent, MediaPlayer** ppPlayer);

	template <class T> static void SafeRelease(T** ppT)
	{
		if (*ppT)
		{
			(*ppT)->Release();
			*ppT = NULL;
		}
	}

	//URIを指定してメディアを開く
	HRESULT OpenFromURI(const WCHAR* sURL);

	//再生を行う。メディアが開かれた状態で実行する。
	HRESULT Play();

	//一時停止を行う。メディアが開かれた状態で実行する。
	HRESULT Pause();

	//このクラスの持つ指定時間分メディアを先に進める。先に進めなかったら進めるところまで進む。
	HRESULT Next();

	//このクラスの持つ指定時間分メディアを後ろに進める。後ろに進めなかったら進めるところまで進む。
	HRESULT Back();

	//このオブジェクトの持つリソースを全て完全に開放する。
	HRESULT ShutDown();

	//現在再生しているメディアの再生位置を変更する。変更した後は一時停止した状態となる。
	HRESULT ChangeCurrentPosition();

	//一時停止している場合にこのメソッドを呼び出すと、現在の再生位置のサムネイルを表示する。
	HRESULT ReDraw();

	//開かれているメディアを指定したサイズに変更する。
	HRESULT ResizeVideo(WORD width, WORD height);

	//開いたメディアが動画を持っているかを返す。
	BOOL HasVideo();

	//開いたメディアが音声を持っているかを返す。
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
	long m_nRefCount; // 参照カウント

	IMFMediaSession* m_pMediaSession;
	IMFMediaSource* m_pMediaSource;
	IMFVideoDisplayControl* m_pVideoDisplayControl;

	HWND m_hWndVideo;          // 映像ウィンドウ
	HWND m_hWndEvent;          // イベントを受け取るためのアプリケーション ウィンドウ
	MediaPlayerState mediaPlayerState; // Media Sessionの現在の状態
	HANDLE m_hCloseEvent;      // 終了中に待機するイベント


};