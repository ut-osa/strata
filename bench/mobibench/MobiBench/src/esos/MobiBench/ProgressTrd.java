package esos.MobiBench;

import android.os.Handler;
import android.os.Message;
import android.util.Log;

public class ProgressTrd extends Thread{
	private boolean run_flag = true;
	private Handler mHandler;
	private Message msg = null;
	private int i = 10;
	private static final String DEBUG_TAG="progress bar";

	ProgressTrd(Handler handler){
		mHandler = handler;
	}

	public void run(){

		while(run_flag) {     
			Log.d(DEBUG_TAG, "[PTrd] - start");
			try {
				while(i > 0){

					Thread.sleep(100); // 1ÃÊ¸¦ ½® µÚ, 
					msg = Message.obtain(mHandler, 0); 
					mHandler.sendMessage(msg);//
					i-=1;
				}
				run_flag = false;

			}catch(InterruptedException e) {
				//
			}
		}
		Log.d(DEBUG_TAG, "[PTrd] - end");
	}



	public void stopThread(){
		run_flag = false;
		synchronized(this){
			this.notify();
		}
	}

}
