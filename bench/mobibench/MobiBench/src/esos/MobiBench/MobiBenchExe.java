package esos.MobiBench;

import java.io.File;

import esos.ResultListControl.DialogActivity;
import android.content.Context;
import android.content.Intent;
import android.graphics.drawable.AnimationDrawable;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.util.Log;
import esos.Database.*;
import esos.MobiBench.TabMain;
public class MobiBenchExe extends Thread{

	private int exp_id;
	private Handler mHandler;
	private static int select_flag = 0;
	private static final String DEBUG_TAG="progress bar";
	private Message msg = null;
	private Context con = null;
	private static Intent intent = null;
	private NotesDbAdapter db = null;
	
	public MobiBenchExe(Context context, Handler handler){
		mHandler = handler;
		con = context;
		

	}

	public void run(){
		int is_error = 0;

		switch(select_flag){
		case 0: // select "ALL"
			this.RunFileIO();
			is_error = (getMobibenchState()==4)?1:0;
			if(is_error != 0) {
				break;
			}
			this.RunSqlite();
			
			intent = new Intent(con,DialogActivity.class);	
			DialogActivity.check_using_db = 1;
			con.startActivity(intent);
			break;
		case 1: // select "File I/O"
			this.RunFileIO();
			is_error = (getMobibenchState()==4)?1:0;
			if(is_error != 0) {
				break;
			}
			intent = new Intent(con,DialogActivity.class);		
			DialogActivity.check_using_db = 1;
			con.startActivity(intent);	
			break;
		case 2: // select "SQLite"
			this.RunSqlite();
			is_error = (getMobibenchState()==4)?1:0;
			if(is_error != 0) {
				break;
			}
			intent = new Intent(con,DialogActivity.class);	
			DialogActivity.check_using_db = 1;
			con.startActivity(intent);

			break;
		case 3: // select "Custom"
			Log.d(DEBUG_TAG, "[RunSQL] - select_flag" + select_flag);	

			this.RunCustom();
			is_error = (getMobibenchState()==4)?1:0;
			if(is_error != 0) {
				break;
			}
			Log.d(DEBUG_TAG, "[RunSQL] - work done");
			
			intent = new Intent(con,DialogActivity.class);		
			DialogActivity.check_using_db = 1;
			con.startActivity(intent);					
			break;
		}	

		DeleteDir(exe_path);

		msg = Message.obtain(mHandler, 444, is_error, 0, null); 
		mHandler.sendMessage(msg);

	}

	MobiBenchExe() {

	}

	public enum eAccessMode {
		WRITE,
		RANDOM_WRITE,
		READ,
		RANDOM_READ
	}

	public enum eDbMode {
		INSERT,
		UPDATE,
		DELETE
	}

	public enum eDbEnable {
		DB_DISABLE,
		DB_ENABLE
	}

	public float cpu_active = 0;
	public float cpu_idle = 0;
	public float cpu_iowait = 0;
	public int cs_total = 0;
	public int cs_voluntary = 0;
	public float throughput = 0;
	public float tps = 0;

	public static String data_path = null;
	public static String sdcard_2nd_path = null;

	static String ExpName[] = {
		"Seq.Write",
		"Seq.read", 
		"Rand.Write",
		"Rand.Read",
		"SQLite.Insert", 
		"SQLite.Update",
		"SQLite.Delete"
	};

	private String exe_path = null;

	public void SetStoragePath(String path) {
		data_path = path;
		sdcard_2nd_path = StorageOptions.determineStorageOptions();
	}

	private void DeleteDir(String path)
	{
		System.out.println("DeleteDir : "+path);   	
		File file = new File(path);
		File[] childFileList = file.listFiles();
		for(File childFile : childFileList)
		{
			if(childFile.isDirectory()) {
				DeleteDir(childFile.getAbsolutePath());    //하위 디렉토리 루프
			}
			else {
				childFile.delete();    //하위 파일삭제
			}
		}

		file.delete();    //root 삭제
	}

	private void RunMobibench(eAccessMode access_mode, eDbEnable db_enable, eDbMode db_mode) {
		Setting set = new Setting();
		int part = set.get_target_partition();
		int exp_id = 0;

		if(db_enable == eDbEnable.DB_DISABLE)
		{
			if(access_mode == eAccessMode.WRITE)
				exp_id = 0;
			else if(access_mode == eAccessMode.READ)
				exp_id = 1;
			else if(access_mode == eAccessMode.RANDOM_WRITE)
				exp_id = 2;
			else if(access_mode == eAccessMode.RANDOM_READ)
				exp_id = 3;
		}
		else
		{
			if(db_mode == eDbMode.INSERT)
				exp_id = 4;
			else if(db_mode == eDbMode.UPDATE)
				exp_id = 5;
			else
				exp_id = 6;
		}

		StartThread(exp_id);

		String partition;

		if(part == 0) {
			partition = data_path;
		} else if(part == 1) {	
			partition = Environment.getExternalStorageDirectory().getPath();
		} else {
			partition = sdcard_2nd_path;
		}

		String command = "mobibench";
		exe_path = partition+"/mobibench";
		command += " -p "+exe_path;

		if(db_enable == eDbEnable.DB_DISABLE) {
			if(access_mode == eAccessMode.WRITE || access_mode == eAccessMode.RANDOM_WRITE) {
				command += " -f "+set.get_filesize_write()*1024;
			} else {
				command += " -f "+set.get_filesize_read()*1024;
			}
			command += " -r "+set.get_io_size();
			command += " -a "+access_mode.ordinal();
			command += " -y "+set.get_file_sync_mode();
			command += " -t "+set.get_thread_num();
		} else {
			command += " -d "+db_mode.ordinal();
			command += " -n "+set.get_transaction_num();
			//command += " -n "+1000;
			command += " -j "+set.get_journal_mode();
			command += " -s "+set.get_sql_sync_mode();
		}

		System.out.println("mobibench command : "+ command);

		mobibench_run(command);

		JoinThread();

		SendResult(exp_id);
	}

	native void mobibench_run(String str);
	native int getMobibenchProgress();
	native int getMobibenchState();

	public void LoadEngine() {
		System.loadLibrary("mobibench");    
	}

	public void printResult() {
		System.out.println("mobibench cpu_active : "+ cpu_active);
		System.out.println("mobibench cpu_idle : "+ cpu_idle);
		System.out.println("mobibench cpu_iowait : "+ cpu_iowait);
		System.out.println("mobibench cs_total : "+ cs_total);
		System.out.println("mobibench cs_voluntary : "+ cs_voluntary);
		System.out.println("mobibench throughput : "+ throughput);
		System.out.println("mobibench tps : "+ tps);   	
	}

	public void SendResult(int result_id) {
		printResult();
		switch(select_flag){
		case 0:
			DialogActivity.ResultType[result_id] = " ▣ Test: All        "; 
			break;
		case 1:
			DialogActivity.ResultType[result_id] = " ▣ Test: File IO"; 
			break;
		case 2:
			DialogActivity.ResultType[result_id] = " ▣ Test: SQLite"; 
			break;
		case 3:
			DialogActivity.ResultType[result_id] = " ▣ Test: My test";
			break;
		}

		DialogActivity.ResultCPU_act[result_id] = String.format("%.0f", cpu_active);
		DialogActivity.ResultCPU_iow[result_id] = String.format("%.0f", cpu_iowait);
		DialogActivity.ResultCPU_idl[result_id] = String.format("%.0f", cpu_idle);
		DialogActivity.ResultCS_tot[result_id] = ""+cs_total;
		DialogActivity.ResultCS_vol[result_id] = ""+cs_voluntary;
		if(result_id < 4) {	// File IO
			if(result_id < 2)	// Sequential
			{
				DialogActivity.ResultThrp[result_id] = String.format("%.0f KB/s", throughput);
			}
			else	// Random
			{
				Setting set = new Setting();
				DialogActivity.ResultThrp[result_id] = String.format("%.0f IOPS(%dKB)", throughput, set.get_io_size());
			}
		} else {	// SQLite
			DialogActivity.ResultThrp[result_id] = String.format("%.0f TPS", tps);
		}
		DialogActivity.ResultExpName[result_id] = ExpName[result_id];
		DialogActivity.bHasResult[result_id]=1;
	}

	public void RunFileIO() {
		int is_error = 0;
		RunMobibench(eAccessMode.WRITE, eDbEnable.DB_DISABLE, eDbMode.INSERT);
		is_error = (getMobibenchState()==4)?1:0;
		if(is_error != 0) {
			return;
		}
		RunMobibench(eAccessMode.READ, eDbEnable.DB_DISABLE, eDbMode.INSERT);
		is_error = (getMobibenchState()==4)?1:0;
		if(is_error != 0) {
			return;
		}
		RunMobibench(eAccessMode.RANDOM_WRITE, eDbEnable.DB_DISABLE, eDbMode.INSERT);
		is_error = (getMobibenchState()==4)?1:0;
		if(is_error != 0) {
			return;
		}
		RunMobibench(eAccessMode.RANDOM_READ, eDbEnable.DB_DISABLE, eDbMode.INSERT);

	}

	public void RunSqlite() {
		int is_error = 0;
		RunMobibench(eAccessMode.WRITE, eDbEnable.DB_ENABLE, eDbMode.INSERT);
		is_error = (getMobibenchState()==4)?1:0;
		if(is_error != 0) {
			return;
		}
		RunMobibench(eAccessMode.WRITE, eDbEnable.DB_ENABLE, eDbMode.UPDATE);
		is_error = (getMobibenchState()==4)?1:0;
		if(is_error != 0) {
			return;
		}
		RunMobibench(eAccessMode.WRITE, eDbEnable.DB_ENABLE, eDbMode.DELETE);

	}

	public void RunCustom() {
		int is_error = 0;
		Setting set = new Setting();
		if(set.get_seq_write() == true) {
			RunMobibench(eAccessMode.WRITE, eDbEnable.DB_DISABLE, eDbMode.INSERT);    
			is_error = (getMobibenchState()==4)?1:0;
			if(is_error != 0) {
				return;
			}
		}  	
		if(set.get_seq_read() == true) {
			RunMobibench(eAccessMode.READ, eDbEnable.DB_DISABLE, eDbMode.INSERT);
			is_error = (getMobibenchState()==4)?1:0;
			if(is_error != 0) {
				return;
			}
		}
		if(set.get_ran_write() == true) {
			RunMobibench(eAccessMode.RANDOM_WRITE, eDbEnable.DB_DISABLE, eDbMode.INSERT);
			is_error = (getMobibenchState()==4)?1:0;
			if(is_error != 0) {
				return;
			}
		}     	
		if(set.get_ran_read() == true) {
			RunMobibench(eAccessMode.RANDOM_READ, eDbEnable.DB_DISABLE, eDbMode.INSERT);
			is_error = (getMobibenchState()==4)?1:0;
			if(is_error != 0) {
				return;
			}
		}  
		if(set.get_insert() == true) {
			RunMobibench(eAccessMode.WRITE, eDbEnable.DB_ENABLE, eDbMode.INSERT);
			is_error = (getMobibenchState()==4)?1:0;
			if(is_error != 0) {
				return;
			}
		}
		if(set.get_update() == true) {
			RunMobibench(eAccessMode.WRITE, eDbEnable.DB_ENABLE, eDbMode.UPDATE);
			is_error = (getMobibenchState()==4)?1:0;
			if(is_error != 0) {
				return;
			}
		}  
		if(set.get_delete() == true) {
			RunMobibench(eAccessMode.WRITE, eDbEnable.DB_ENABLE, eDbMode.DELETE);
		}    

	}

	public void setMobiBenchExe(int flag){
		select_flag = flag;

		Log.d(DEBUG_TAG, "MBE - select flag is " + select_flag);
	}


	public boolean runflag = false;

	public class ProgThread extends Thread{

		public void run(){
			int prog = 0;
			int stat = 0;
			int old_prog = 0;
			int old_stat = -1;

			msg = Message.obtain(mHandler, 0); 
			mHandler.sendMessage(msg);

			runflag = true;
			while(runflag) {
				prog = getMobibenchProgress();
				stat = getMobibenchState();
				/*
				 * state
				 * 0 : NONE
				 * 1 : READY
				 * 2 : EXE
				 * 3 : END
				 */
				if(prog > old_prog || prog == 0 || old_stat != stat) {
					msg = Message.obtain(mHandler, prog); 
					mHandler.sendMessage(msg);	
					old_prog = prog;
				}

				if(stat < 2) {
					msg = Message.obtain(mHandler, 999, 0, 0, "Initializing for "+ExpName[exp_id]);
				} else {
					msg = Message.obtain(mHandler, 999, 0, 0, "Executing "+ExpName[exp_id]);
				}
				mHandler.sendMessage(msg);
				old_stat = stat;

				try {
					sleep(10);
				} catch (InterruptedException e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}
			}

			if (stat == 4) {
				msg = Message.obtain(mHandler, 0); 
				mHandler.sendMessage(msg);

				msg = Message.obtain(mHandler, 999, 0, 0, ExpName[exp_id]+" exited with error");
				mHandler.sendMessage(msg);
			} else {
				msg = Message.obtain(mHandler, 100); 
				mHandler.sendMessage(msg);

				msg = Message.obtain(mHandler, 999, 0, 0, ExpName[exp_id]+" done");
				mHandler.sendMessage(msg);

			}


		}
	}

	public Thread thread;

	public void StartThread(int id) {

		exp_id = id;
		thread =  new ProgThread();

		thread.start();

	}

	public void JoinThread() {
		runflag = false;
		try{
			thread.join();	
		}catch(InterruptedException e){
			e.printStackTrace();
		}			
	}
	public void stopThread(){
		runflag = false;

		synchronized(this){
			this.notify();
		}
	}


}
