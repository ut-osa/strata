package esos.ResultListControl;

import esos.MobiBench.R;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.os.Bundle;
import android.view.KeyEvent;
import android.webkit.WebView;
import android.webkit.WebViewClient;


public class Webview extends Activity{

	private WebView mWebView;
	@SuppressLint("SetJavaScriptEnabled")
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.mbwebview);

		setLayout();

		// 웹뷰에서 자바스크립트실행가능
		mWebView.getSettings().setJavaScriptEnabled(true); 


		if(DialogActivity.G_EXP_CHOICE.equals("Seq.Write")){
			mWebView.loadUrl("http://mobibench.dothome.co.kr/rank_seq_write.php?mysn=" + DialogActivity.dev_num);
		}else if(DialogActivity.G_EXP_CHOICE.equals("Seq.read")){
			mWebView.loadUrl("http://mobibench.dothome.co.kr/rank_seq_read.php?mysn="+ DialogActivity.dev_num);
		}else if(DialogActivity.G_EXP_CHOICE.equals("Rand.Write")){
			mWebView.loadUrl("http://mobibench.dothome.co.kr/rank_ran_write.php?mysn="+ DialogActivity.dev_num);
		}else if(DialogActivity.G_EXP_CHOICE.equals("Rand.Read")){
			mWebView.loadUrl("http://mobibench.dothome.co.kr/rank_ran_read.php?mysn="+ DialogActivity.dev_num);
		}else if(DialogActivity.G_EXP_CHOICE.equals("SQLite.Insert")){
			mWebView.loadUrl("http://mobibench.dothome.co.kr/rank_sqlite_insert.php?mysn="+ DialogActivity.dev_num);
		}else if(DialogActivity.G_EXP_CHOICE.equals("SQLite.Update")){
			mWebView.loadUrl("http://mobibench.dothome.co.kr/rank_sqlite_update.php?mysn="+ DialogActivity.dev_num);
		}else if(DialogActivity.G_EXP_CHOICE.equals("SQLite.Delete")){
			mWebView.loadUrl("http://mobibench.dothome.co.kr/rank_sqlite_delete.php?mysn="+ DialogActivity.dev_num);
		}else{
			mWebView.loadUrl("http://mobibench.co.kr/");
		}

		// WebViewClient 지정
		mWebView.setWebViewClient(new WebViewClientClass());  

	}

	@Override
	public boolean onKeyDown(int keyCode, KeyEvent event) { 
		if ((keyCode == KeyEvent.KEYCODE_BACK) && mWebView.canGoBack()) { 
			mWebView.goBack(); 
			return true; 
		} 
		return super.onKeyDown(keyCode, event);
	}

	private class WebViewClientClass extends WebViewClient { 
		public boolean shouldOverrideUrlLoading(WebView view, String url) { 
			view.loadUrl(url); 
			return true; 
		} 
	}

	/*
	 * Layout
	 */
	 private void setLayout(){
		 mWebView = (WebView) findViewById(R.id.webview);
	 }
}
