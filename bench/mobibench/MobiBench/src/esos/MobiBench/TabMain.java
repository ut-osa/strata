
package esos.MobiBench;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.math.BigInteger;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import esos.Database.*;
import esos.MobiBench.R;
import esos.ResultListControl.DialogActivity;
import android.app.AlertDialog;
import android.app.TabActivity;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager.NameNotFoundException;
import android.database.Cursor;
import android.graphics.drawable.AnimationDrawable;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.telephony.TelephonyManager;
import android.util.Log;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.CompoundButton.OnCheckedChangeListener;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.TabHost;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.AdapterView.OnItemSelectedListener;



public class TabMain extends TabActivity {
	// for update version check

	private String mResult ="1";


	private int sVersion;
	private int sMyVersion;

	private SharedPreferences prefs = null;
	private SharedPreferences.Editor editor = null;
	private int db_index = 0;

	private boolean root_flag;

	private Setting set = new Setting();

	private CheckBox CB_SW = null;
	private CheckBox CB_SR = null;
	private CheckBox CB_RW = null;
	private CheckBox CB_RR = null;
	private CheckBox CB_INSERT = null;
	private CheckBox CB_UPDATE = null;
	private CheckBox CB_DELETE = null;

	private EditText et_threadnum = null;
	private EditText et_filesize_w = null;
	private EditText et_filesize_r = null;
	//private EditText et_io_size = null;
	private EditText et_transaction = null;

	private Spinner sp_partition = null;
	private Spinner sp_file_sync= null;
	private Spinner sp_io_size=null;
	private Spinner sp_sql_sync= null;
	private Spinner sp_journal= null;
	private MobiBenchExe m_exe = null;	
	private TextView tv_progress_txt = null;
	private TextView tv_progress_per = null;
	private TextView TV_free_space = null;



	private boolean mFlag = false; // using App stop button

	private Cursor result = null;
	private String db_date = null;
	private final ArrayList<String> arr = new ArrayList<String>();	
	private ArrayAdapter<String> aa = null;
	private static int result_start = 0;
	private String tmpExpName[] = {
			"Seq.Write",
			"Seq.read", 
			"Rand.Write",
			"Rand.Read",
			"SQLite.Insert", 
			"SQLite.Update",
			"SQLite.Delete"
	};

	//   ProgressTrd thread = null;
	MobiBenchExe mb_thread= null;

	/*jwgom*/

	private static final String DEBUG_TAG="progress bar";

	public static ProgressBar prBar = null;
	private Context con;

	private static boolean btn_clk_check = true;

	private static long free_space = 0;
	private static String free_suffix = null;

	
	/* For Animation*/
	public AnimationDrawable anidrawable;
	public static boolean g_animation = true;
	public ImageView image = null;
	
	Handler mHandler = new Handler(){
		public void handleMessage(Message msg){

			if(msg.what <= 100)
			{
				prBar.setProgress(msg.what);   		
				tv_progress_per.setText(""+msg.what+"%");
			}
			else if(msg.what == 999)
			{
				tv_progress_txt.setText((String)msg.obj);
			}
			else if(msg.what == 666)
			{
				mFlag = false;
			}
			else if(msg.what == 444)
			{
				//while(image.getId() != R.drawable.bg_mea_middle06 ){
					anidrawable.stop();
				//}
				
				image.setImageResource(R.drawable.bg_mea_middle06);
				if(msg.arg1 == 1)
				{
					print_error(1);
				}

				Log.d(DEBUG_TAG, "[JWGOM] join start");
				try {
					mb_thread.join();
				} catch (InterruptedException e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}
				Log.d(DEBUG_TAG, "[JWGOM] join end");

				btn_clk_check = true;
				
			}
		}
	};	

	static final int PROGRESS_DIALOG = 0;

	// For Database
	public static NotesDbAdapter dbAdapter;
	

	
	protected void onCreate(Bundle savedInstanceState){
		super.onCreate(savedInstanceState);
		Log.d(DEBUG_TAG, "**********onCreate");

		if(m_exe == null) {
			m_exe = new MobiBenchExe();
			m_exe.LoadEngine();
			m_exe.SetStoragePath(this.getFilesDir().toString());
		}		

		// Get device information
		DialogActivity.dev_num =getMD5Hash(getDeviceId());


		// For Database
		dbAdapter = new NotesDbAdapter(this);
		dbAdapter.open();

		/* For tab layout setting */
		TabHost tabHost = getTabHost();			
		LayoutInflater.from(this).inflate(R.layout.tabmain,tabHost.getTabContentView(), true);			
		tabHost.addTab(tabHost.newTabSpec("measure").setIndicator("",getResources().getDrawable(R.drawable.tab_mea)).setContent(R.id.measure));
		tabHost.addTab(tabHost.newTabSpec("history").setIndicator("",getResources().getDrawable(R.drawable.tab_history)).setContent(R.id.history));
		tabHost.addTab(tabHost.newTabSpec("setting").setIndicator("",getResources().getDrawable(R.drawable.tab_help)).setContent(R.id.help));

		/* For Animation*/
		image = (ImageView)findViewById(R.id.position_mea);
		image.setBackgroundResource(R.anim.aniimage);
		anidrawable = (AnimationDrawable)image.getBackground();
		//image.post(new StartAni());


		
		
		
		/* Preference Control */
		prefs = getSharedPreferences("Setting", MODE_PRIVATE);
		root_flag = prefs.getBoolean("init_flag", true);
		editor = prefs.edit();

		/*jwgom
		 * 
		 * ***************************************************************************************************************************************************
		 * */
		PackageInfo pi = null;

		try {

			pi = getPackageManager().getPackageInfo(getPackageName(), 0);

		} catch (NameNotFoundException e) {

			// TODO Auto-generated catch block

			e.printStackTrace();

		}

		String verSion = pi.versionName;
		int verCode = pi.versionCode;
		Log.d(DEBUG_TAG, "versionName is " + verSion);
		Log.d(DEBUG_TAG, "versionCode is " + verCode);

		/*jwgom
		 * 
		 * ***************************************************************************************************************************************************
		 * */		



		db_index = prefs.getInt("database_index", 0); // data base indexing

		result = null;
		String db_data = null;
		final ArrayList<String> arr = new ArrayList<String>();
		Log.d(DEBUG_TAG, "**********onCreate before jwgom  1");

		if(db_index != 0 ){
			for(int i=0; i<db_index; i++){
				result = dbAdapter.fetchNote(i);// 횟수 제한
				result.moveToFirst();
				db_data = "  " + result.getString(2) + "(" + result.getString(1) + ")";
				arr.add(db_data);
			}
			result.close();

		}


		ListView list = (ListView) findViewById(R.id.ListView01);  
		aa = new ArrayAdapter<String> ( this, R.layout.history_listitem, arr);                  
		list.setAdapter(aa);                                                              // ListView에 ArrayAdapter 설정


		list.setOnItemClickListener(new OnItemClickListener() { public void onItemClick
			(AdapterView<?> a, View v, int position, long id) {

			result = dbAdapter.fetchNote(position);
			result.moveToFirst();
			//for(int i=0; i < 7; i++) {


			Log.d(DEBUG_TAG, "Start cursor position " + result.getPosition());


			// clear values
			result_start = 0;
			DialogActivity.ResultDate = null;
			for(int i=0; i < 7; i++) {
				DialogActivity.bHasResult[i]=0;
				DialogActivity.ResultCPU_act[i]=null;
				DialogActivity.ResultCPU_iow[i]=null;
				DialogActivity.ResultCPU_idl[i]=null;
				DialogActivity.ResultCS_tot[i]=null;
				DialogActivity.ResultCS_vol[i]=null;
				DialogActivity.ResultThrp[i]=null;
				DialogActivity.ResultExpName[i]=null;
				DialogActivity.ResultType[i] = null;
			}



			DialogActivity.ResultDate = result.getString(1);
			while(!result.isAfterLast()){
				Log.d(DEBUG_TAG, "Create DialogActivity (position/result_start/expname) " + result.getPosition() + " " + result_start + " " + result.getString(10));

				if(result.getString(10).equals(tmpExpName[0] )){ // seq write
					result_start = 0;
				}else if(result.getString(10).equals(tmpExpName[1] )){
					result_start = 1;
				}else if(result.getString(10).equals(tmpExpName[2] )){
					result_start = 2;
				}else if(result.getString(10).equals(tmpExpName[3] )){
					result_start = 3;
				}else if(result.getString(10).equals(tmpExpName[4] )){
					result_start = 4;
				}else if(result.getString(10).equals(tmpExpName[5] )){
					result_start = 5;
				}else if(result.getString(10).equals(tmpExpName[6] )){
					result_start = 6;
				}
				DialogActivity.bHasResult[result_start] = result.getInt(3);
				DialogActivity.ResultCPU_act[result_start]= result.getString(4);
				DialogActivity.ResultCPU_iow[result_start]= result.getString(5);
				DialogActivity.ResultCPU_idl[result_start]= result.getString(6);
				DialogActivity.ResultCS_tot[result_start]= result.getString(7);
				DialogActivity.ResultCS_vol[result_start]= result.getString(8);
				DialogActivity.ResultThrp[result_start]= result.getString(9);
				DialogActivity.ResultExpName[result_start]= result.getString(10);
				DialogActivity.ResultType[result_start] = result.getString(2);
				result.moveToNext();

			}

			Intent intent = new Intent(TabMain.this, DialogActivity.class);
			DialogActivity.check_using_db = 0;
			startActivity(intent);              

		}
		});

		/*jwgom
		 * 
		 * ***************************************************************************************************************************************************
		 * */        
		Log.d(DEBUG_TAG, "**********onCreate after jwgom");

		/* spinner define (total 5 spinner) */
		sp_partition = (Spinner)findViewById(R.id.sp_partition);
		sp_file_sync = (Spinner)findViewById(R.id.sp_file_sync);
		sp_io_size = (Spinner)findViewById(R.id.sp_io_size);
		sp_sql_sync = (Spinner)findViewById(R.id.sp_sql_sync);
		sp_journal = (Spinner)findViewById(R.id.sp_journal);

		ArrayAdapter ad_partition;
		if(StorageOptions.b_2nd_sdcard == true) {
			ad_partition = ArrayAdapter.createFromResource(this, R.array.partition, android.R.layout.simple_spinner_item);
		} else {	
			ad_partition = ArrayAdapter.createFromResource(this, R.array.partition2, android.R.layout.simple_spinner_item);
		}
		ArrayAdapter ad_file_sync = ArrayAdapter.createFromResource(this, R.array.filesyncmode,R.layout.spinner_item);
		ArrayAdapter ad_io_size = ArrayAdapter.createFromResource(this, R.array.iosize,R.layout.spinner_item);
		ArrayAdapter ad_sql_sync = ArrayAdapter.createFromResource(this, R.array.sqlsyncmode,R.layout.spinner_item);
		ArrayAdapter ad_journal = ArrayAdapter.createFromResource(this, R.array.journalmode,R.layout.spinner_item);
		//		
		ad_partition.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		ad_file_sync.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		ad_io_size.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		ad_sql_sync.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		ad_journal.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);

		sp_partition.setAdapter(ad_partition);
		sp_file_sync.setAdapter(ad_file_sync);
		sp_io_size.setAdapter(ad_io_size);
		sp_sql_sync.setAdapter(ad_sql_sync);
		sp_journal.setAdapter(ad_journal);

		et_threadnum = (EditText)findViewById(R.id.threadnum);
		et_filesize_w = (EditText)findViewById(R.id.filesize_w);
		et_filesize_r = (EditText)findViewById(R.id.filesize_r);
		//et_io_size = (EditText)findViewById(R.id.io_size);
		et_transaction = (EditText)findViewById(R.id.transcation);

		CB_SW=(CheckBox)findViewById(R.id.cb_sw);
		CB_SR=(CheckBox)findViewById(R.id.cb_sr);
		CB_RW=(CheckBox)findViewById(R.id.cb_rw);
		CB_RR=(CheckBox)findViewById(R.id.cb_rr);
		CB_INSERT=(CheckBox)findViewById(R.id.cb_insert);
		CB_UPDATE=(CheckBox)findViewById(R.id.cb_update);
		CB_DELETE=(CheckBox)findViewById(R.id.cb_delete);		

		tv_progress_txt = (TextView)findViewById(R.id.progress_text);
		tv_progress_per = (TextView)findViewById(R.id.progress_per);

		prBar = (ProgressBar)findViewById(R.id.progress);
		prBar.setProgress(0);




		/* First Warning message control */
		if( root_flag ){
			set_default();	


			startActivityForResult(new Intent(TabMain.this, First.class), 0);	
		}else{
			load_init();

			// for version check and update this app.

			try {
				URL url = new URL("http://mobibench.dothome.co.kr/mobibench_ver.html");
				HttpURLConnection conn = (HttpURLConnection)url.openConnection();
				if(conn != null) {
					conn.setConnectTimeout(10000);
					conn.setUseCaches(false);
					if(conn.getResponseCode() == HttpURLConnection.HTTP_OK) {
						BufferedReader br = new BufferedReader(
								new InputStreamReader(conn.getInputStream()));
						Boolean bVersion = false;
						for(;;) {

							String line = br.readLine();
							Log.d(DEBUG_TAG, "line" + line);
							if(bVersion){
								mResult = line;
								Log.d(DEBUG_TAG, "get mResult" + mResult);
								break;
							}
							if(line.equals("version:")){
								bVersion = true;
							}
							if (line == null){ 
								break;
							}	
						}
						br.close();
					}
					conn.disconnect();
				}
			} catch (Exception e) {;}

			sVersion = Integer.parseInt(mResult);

			try {
				PackageInfo i_tmp = this.getPackageManager().getPackageInfo(this.getPackageName(), 0);
				sMyVersion = i_tmp.versionCode;
			}catch(NameNotFoundException e){ }


			ConnectivityManager cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
			NetworkInfo ni = cm.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
			boolean isWifiAvail = ni.isAvailable();
			boolean isWifiConn = ni.isConnected();
			ni = cm.getNetworkInfo(ConnectivityManager.TYPE_MOBILE);
			boolean isMobileAvail = ni.isAvailable();
			boolean isMobileConn = ni.isConnected();

			String status = "WiFi\nAvail = " + isWifiAvail + "\nConn = "
					+ isWifiConn + "\nMobile\nAvail = " + isMobileAvail
					+ "\nConn = " + isMobileConn + "\n";
			if (isWifiConn || isMobileConn) {
				if(sVersion != sMyVersion){
					Log.d(DEBUG_TAG, "update sVersion is " + sVersion);
					Log.d(DEBUG_TAG, "update sMyVersion is " + sMyVersion);
					AlertDialog alert =  new AlertDialog.Builder(this)
					.setTitle("Notice.")
					.setMessage("Your Mobibench is not the latest version. Do you want to update your app?")
					.setCancelable(true)
					.setPositiveButton("Update", new DialogInterface.OnClickListener() {
						public void onClick(DialogInterface dialog, int whichButton) {	          
							Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse("market://details?id=esos.MobiBench"));
							startActivity(intent);
						}
					})
					.setNegativeButton("Later", new DialogInterface.OnClickListener() {
						public void onClick(DialogInterface dialog, int whichButton) {

						}
					})
					.show();	  
				}	        	
			}

		}

		String target_path = null;

		switch(set.get_target_partition()) {
		case 0:
			target_path = Environment.getDataDirectory().getPath();
			break;
		case 1:
			target_path = Environment.getExternalStorageDirectory().getPath();
			break;
		case 2:
			target_path = MobiBenchExe.sdcard_2nd_path;
			break;
		}


		free_space = StorageOptions.getAvailableSize(target_path);
		free_suffix = StorageOptions.formatSize(free_space);

		TV_free_space = (TextView)findViewById(R.id.freespace);
		TV_free_space.setText("("+free_suffix+" free)"); 	

		// Activity가 실행 중인 동안 화면을 밝게 유지합니다.
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

		/* Image button listener*/
		findViewById(R.id.btn_execute).setOnClickListener(mClickListener);
		findViewById(R.id.btn_all).setOnClickListener(mClickListener);
		findViewById(R.id.btn_file).setOnClickListener(mClickListener);
		findViewById(R.id.btn_sqlite).setOnClickListener(mClickListener);
		findViewById(R.id.btn_custom).setOnClickListener(mClickListener);

		/* ******************* */
		/*   Spinner Control   */
		/* ******************* */
		// Partition spinner
		sp_partition.setOnItemSelectedListener(
				new OnItemSelectedListener(){
					public void onItemSelected(AdapterView<?> parent,View view, int position, long id){
						String target_path = null;

						switch(position){ 
						case 0:
							editor.putInt("p_target_partition", 0);					
							set.set_target_partition(0);
							target_path = Environment.getDataDirectory().getPath();
							break;
						case 1:
							editor.putInt("p_target_partition", 1);	
							set.set_target_partition(1);
							target_path = Environment.getExternalStorageDirectory().getPath();
							break;
						case 2:
							editor.putInt("p_target_partition", 2);	
							set.set_target_partition(2);
							target_path = m_exe.sdcard_2nd_path;
							break;					
						}
						editor.commit();	

						free_space = StorageOptions.getAvailableSize(target_path);
						free_suffix = StorageOptions.formatSize(free_space);

						TV_free_space.setText("("+free_suffix+" free)");
					}
					public void onNothingSelected(AdapterView<?> parent){
						//	
					}		
				}
				);

		// File synchronization spinner
		sp_file_sync.setOnItemSelectedListener(
				new OnItemSelectedListener(){
					public void onItemSelected(AdapterView<?> parent,View view, int position, long id){
						switch(position){ 
						case 0:
							editor.putInt("p_file_sync_mode", 0);	
							set.set_file_sync_mode(0);
							break;
						case 1:
							editor.putInt("p_file_sync_mode", 1);	
							set.set_file_sync_mode(1);
							break;
						case 2:
							editor.putInt("p_file_sync_mode", 2);	
							set.set_file_sync_mode(2);
							break;
						case 3:
							editor.putInt("p_file_sync_mode", 3);	
							set.set_file_sync_mode(3);
							break;
						case 4:
							editor.putInt("p_file_sync_mode", 4);	
							set.set_file_sync_mode(4);
							break;
						case 5:
							editor.putInt("p_file_sync_mode", 5);	
							set.set_file_sync_mode(5);
							break;
						case 6:
							editor.putInt("p_file_sync_mode", 6);	
							set.set_file_sync_mode(6);
							break;
						case 7:
							editor.putInt("p_file_sync_mode", 7);	
							set.set_file_sync_mode(7);
							break;
						}
						editor.commit();
					}
					public void onNothingSelected(AdapterView<?> parent){
						//	
					}
				}
				);

		// IO size spinner
		sp_io_size.setOnItemSelectedListener(
				new OnItemSelectedListener(){
					public void onItemSelected(AdapterView<?> parent,View view, int position, long id){
						switch(position){ 
						case 0:
							editor.putInt("p_io_size", 0);	
							set.set_io_size(0);
							break;
						case 1:
							editor.putInt("p_io_size", 1);	
							set.set_io_size(1);
							break;
						case 2:
							editor.putInt("p_io_size", 2);	
							set.set_io_size(2);
							break;
						case 3:
							editor.putInt("p_io_size", 3);	
							set.set_io_size(3);
							break;
						case 4:
							editor.putInt("p_io_size", 4);	
							set.set_io_size(4);
							break;
						}
						editor.commit();
					}
					public void onNothingSelected(AdapterView<?> parent){
						//	
					}
				}
				);		

		// SQLite synchronization spinner
		sp_sql_sync.setOnItemSelectedListener(
				new OnItemSelectedListener(){
					public void onItemSelected(AdapterView<?> parent,View view, int position, long id){
						switch(position){ 
						case 0:
							editor.putInt("p_sql_sync_mode", 0);	
							set.set_sql_sync_mode(0);
							break;
						case 1:
							editor.putInt("p_sql_sync_mode", 1);
							set.set_sql_sync_mode(1);
							break;
						case 2:
							editor.putInt("p_sql_sync_mode", 2);
							set.set_sql_sync_mode(2);
							break;
						}
						editor.commit();
					}
					public void onNothingSelected(AdapterView<?> parent){
						//	
					}
				}
				);			

		// SQL journaling spinner
		sp_journal.setOnItemSelectedListener(
				new OnItemSelectedListener(){
					public void onItemSelected(AdapterView<?> parent,View view, int position, long id){
						switch(position){ 
						case 0:
							editor.putInt("p_journal_mode", 0);
							set.set_journal_mode(0);
							break;
						case 1:
							editor.putInt("p_journal_mode", 1);
							set.set_journal_mode(1);
							break;
						case 2:
							editor.putInt("p_journal_mode", 2);
							set.set_journal_mode(2);
							break;
						case 3:
							editor.putInt("p_journal_mode", 3);
							set.set_journal_mode(3);
							break;
						case 4:
							editor.putInt("p_journal_mode", 4);
							set.set_journal_mode(4);
							break;
						case 5:
							editor.putInt("p_journal_mode", 5);
							set.set_journal_mode(5);
							break;						
						}
						editor.commit();
					}
					public void onNothingSelected(AdapterView<?> parent){
						//	
					}
				}
				);	


		/* ******************* */
		/*  Check box control  */
		/* ******************* */
		// Sequential Write Check box
		CB_SW = (CheckBox)findViewById(R.id.cb_sw);
		CB_SW.setOnCheckedChangeListener(
				new OnCheckedChangeListener(){
					public void onCheckedChanged(CompoundButton buttonView, boolean isChecked){
						if(isChecked == true){
							set.set_seq_write(true);

						}else{
							set.set_seq_write(false); 

						}
					}   	
				});	

		// Sequential Read Check box
		CB_SR = (CheckBox)findViewById(R.id.cb_sr);
		CB_SR.setOnCheckedChangeListener(
				new OnCheckedChangeListener(){
					public void onCheckedChanged(CompoundButton buttonView, boolean isChecked){
						if(isChecked == true){
							set.set_seq_read(true);

						}else{
							set.set_seq_read(false);

						}
					}   	
				});	
		// Random Write Check box
		CB_RW = (CheckBox)findViewById(R.id.cb_rw);
		CB_RW.setOnCheckedChangeListener(
				new OnCheckedChangeListener(){
					public void onCheckedChanged(CompoundButton buttonView, boolean isChecked){
						if(isChecked == true){
							set.set_ran_write(true);

						}else{
							set.set_ran_write(false);

						}
					}   	
				});	

		// Random Read Check box
		CB_RR = (CheckBox)findViewById(R.id.cb_rr);
		CB_RR.setOnCheckedChangeListener(
				new OnCheckedChangeListener(){
					public void onCheckedChanged(CompoundButton buttonView, boolean isChecked){
						if(isChecked == true){
							set.set_ran_read(true);

						}else{
							set.set_ran_read(false);

						}
					}   	
				});	

		// SQLite Insert Check box
		CB_INSERT = (CheckBox)findViewById(R.id.cb_insert);
		CB_INSERT.setOnCheckedChangeListener(
				new OnCheckedChangeListener(){
					public void onCheckedChanged(CompoundButton buttonView, boolean isChecked){
						if(isChecked == true){
							set.set_insert(true);

						}else{
							set.set_insert(false);

						}
					}   	
				});	

		// Sequential Write Check box
		CB_UPDATE = (CheckBox)findViewById(R.id.cb_update);
		CB_UPDATE.setOnCheckedChangeListener(
				new OnCheckedChangeListener(){
					public void onCheckedChanged(CompoundButton buttonView, boolean isChecked){
						if(isChecked == true){
							set.set_update(true);

						}else{
							set.set_update(false);

						}
					}   	
				});	

		// Sequential Write Check box
		CB_DELETE = (CheckBox)findViewById(R.id.cb_delete);
		CB_DELETE.setOnCheckedChangeListener(
				new OnCheckedChangeListener(){
					public void onCheckedChanged(CompoundButton buttonView, boolean isChecked){
						if(isChecked == true){
							set.set_delete(true);

						}else{
							set.set_delete(false);

						}
					}   	
				});	



		//        mb_thread.setDaemon(true);
		con = this;
		Log.d(DEBUG_TAG, "**********onCreate complete");

	}
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		if(requestCode == 0) {
			if(resultCode == RESULT_CANCELED) {
				this.finish();
			}
		}
	}

	public boolean onKeyDown(int keyCode, KeyEvent event) {
		if (keyCode == KeyEvent.KEYCODE_BACK) {
			if(!mFlag) {
				Toast.makeText(TabMain.this, "Press the \"Back button\" again to exit", Toast.LENGTH_SHORT).show();
				mFlag = true;
				mHandler.sendEmptyMessageDelayed(666, 2000);
				return false;
			} else {
				setResult(RESULT_CANCELED);
				storeValue();

				this.finish();
			}
		}
		return super.onKeyDown(keyCode, event);
	}


	private void startMobibenchExe(int type) {
		if(btn_clk_check == false){
			print_error(0);
		}else{
			image.post(new StartAni());
			btn_clk_check = false;
			Log.d(DEBUG_TAG, "[TM] BTN_CLICK:TRUE" + "[" + btn_clk_check + "]");
			storeValue();

			if((Integer.parseInt(et_filesize_w.getText().toString()) >= free_space/1024/1024) || (Integer.parseInt(et_filesize_r.getText().toString()) >= free_space/1024/1024))
			{
				print_error(2);
				btn_clk_check = true;
				return;
			}

			set_configuration();
			DialogActivity.g_def = check_default();
			
			DialogActivity.ClearResult(dbAdapter);

			m_exe.setMobiBenchExe(type);	
			print_exp(type);
			mb_thread = new MobiBenchExe(con, mHandler);			
			mb_thread.start();		
		}
	}


	private boolean checkbox_counting(){
		boolean can_run = false;
		if(set.get_seq_write() || set.get_seq_read() || set.get_ran_write()||  set.get_ran_read() || set.get_insert() || set.get_update()||  set.get_delete()){
			can_run = true;
		}else{
			can_run = false;
		}
		return can_run;
	}

	/* Image button listener*/
	Button.OnClickListener mClickListener = new View.OnClickListener() {
		public void onClick(View v) {
			//		Intent intent;		

			switch(v.getId()){
			case R.id.btn_execute:
				set_default();
				load_init();
				break;
			case R.id.btn_all:				
				startMobibenchExe(0);		
				break;
			case R.id.btn_file:				
				startMobibenchExe(1);
				break;
			case R.id.btn_sqlite:
				//image.post(new StartAni());
				// For Database
				startMobibenchExe(2);
				break;
			case R.id.btn_custom:
				//image.post(new StartAni());
				if( btn_clk_check == true && checkbox_counting() == false){
					print_exp(4);
				}else{
					startMobibenchExe(3);					
				}
				break;

			}
		}
	};

	/* Load : stored in preferences */
	/* Load preferenced values */
	private void load_init() {
		if((StorageOptions.b_2nd_sdcard == false) && prefs.getInt("p_target_partition", 0) == 2) {
			sp_partition.setSelection(0);
		} else {
			sp_partition.setSelection(prefs.getInt("p_target_partition", 0));
		}
		et_threadnum.setText(String.valueOf(prefs.getInt("p_threadnum", 1)));
		et_filesize_w.setText(String.valueOf(prefs.getInt("p_filesize_w", 10)));
		et_filesize_r.setText(String.valueOf(prefs.getInt("p_filesize_r", 32)));
		//et_io_size.setText(String.valueOf(prefs.getInt("p_io_size", 4)));
		sp_io_size.setSelection(prefs.getInt("p_io_size", 0));
		sp_file_sync.setSelection(prefs.getInt("p_file_sync_mode", 3));
		et_transaction.setText(String.valueOf(prefs.getInt("p_transaction", 1)));
		sp_sql_sync.setSelection(prefs.getInt("p_sql_sync_mode", 0)); 
		sp_journal.setSelection(prefs.getInt("p_journal_mode", 0));

		if((StorageOptions.b_2nd_sdcard == false) && prefs.getInt("p_target_partition", 0) == 2) {
			set.set_target_partition(0);
		} else {
			set.set_target_partition(prefs.getInt("p_target_partition", 0));
		}
		set.set_thread_num(prefs.getInt("p_threadnum", 1));	
		set.set_filesize_write(prefs.getInt("p_filesize_w", 10));
		set.set_filesize_read(prefs.getInt("p_filesize_r", 32));		
		set.set_io_size(prefs.getInt("p_io_size", 0));
		set.set_file_sync_mode(prefs.getInt("p_file_sync_mode", 3));
		set.set_transaction_num(prefs.getInt("p_transaction", 1));	
		set.set_sql_sync_mode(prefs.getInt("p_sql_sync_mode", 0));
		set.set_journal_mode(prefs.getInt("p_journal_mode", 0));

		/* Check box setting */
		Log.d(DEBUG_TAG, "[JWGOM] start");
		CB_SW.setChecked(prefs.getBoolean("p_cb_sw", false));
		CB_SR.setChecked(prefs.getBoolean("p_cb_sr", false));
		CB_RW.setChecked(prefs.getBoolean("p_cb_rw", false));
		CB_RR.setChecked(prefs.getBoolean("p_cb_rr", false));
		CB_INSERT.setChecked(prefs.getBoolean("p_cb_insert", false));
		CB_UPDATE.setChecked(prefs.getBoolean("p_cb_update", false));
		CB_DELETE.setChecked(prefs.getBoolean("p_cb_delete", false));


		Log.d(DEBUG_TAG, "[JWGOM] end1");
		set.set_seq_write(prefs.getBoolean("p_cb_sw", false));
		set.set_seq_read(prefs.getBoolean("p_cb_sr", false));
		set.set_ran_write(prefs.getBoolean("p_cb_rw", false));
		set.set_ran_read(prefs.getBoolean("p_cb_rr", false));
		set.set_insert(prefs.getBoolean("p_cb_insert", false));
		set.set_update(prefs.getBoolean("p_cb_update", false));
		set.set_delete(prefs.getBoolean("p_cb_delete", false));


		set.set_cb_count(prefs.getInt("p_cb_count", 0));
		//Toast.makeText(this, "load init cb count "+ set.get_cb_count() , Toast.LENGTH_SHORT).show();
		Log.d(DEBUG_TAG, "[JWGOM] end");
		//print_values();


	}

	/* Store values : To the preference */
	public void storeValue() {				
		set.set_thread_num(Integer.parseInt(et_threadnum.getText().toString()));
		editor.putInt("p_threadnum", Integer.parseInt(et_threadnum.getText().toString()));

		//	if(Integer.parseInt(et_filesize_w.getText().toString()) >= free_space/1024/1024)
		if(Integer.parseInt(et_filesize_w.getText().toString()) < free_space/1024/1024)
		{
			set.set_filesize_write(Integer.parseInt(et_filesize_w.getText().toString()));
			editor.putInt("p_filesize_w", Integer.parseInt(et_filesize_w.getText().toString()));
		}else{
			Log.d(DEBUG_TAG, "[JWGOM] storeValue() -> file size write is not saved");
		}

		if(Integer.parseInt(et_filesize_r.getText().toString()) < free_space/1024/1024)
		{
			set.set_filesize_read(Integer.parseInt(et_filesize_r.getText().toString()));
			editor.putInt("p_filesize_r", Integer.parseInt(et_filesize_r.getText().toString()));
		}
		//set.set_io_size(Integer.parseInt(et_io_size.getText().toString()));
		//editor.putInt("p_io_size", Integer.parseInt(et_io_size.getText().toString()));
		set.set_transaction_num(Integer.parseInt(et_transaction.getText().toString()));
		editor.putInt("p_transaction", Integer.parseInt(et_transaction.getText().toString()));

		/* Store : Checkbox */
		set.set_seq_write(CB_SW.isChecked());
		set.set_seq_read(CB_SR.isChecked());
		set.set_ran_write(CB_RW.isChecked());
		set.set_ran_read(CB_RR.isChecked());
		set.set_insert(CB_INSERT.isChecked());
		set.set_update(CB_UPDATE.isChecked());
		set.set_delete(CB_DELETE.isChecked());


		editor.putBoolean("p_cb_sw", CB_SW.isChecked());
		editor.putBoolean("p_cb_sr", CB_SR.isChecked());
		editor.putBoolean("p_cb_rw", CB_RW.isChecked());
		editor.putBoolean("p_cb_rr", CB_RR.isChecked());
		editor.putBoolean("p_cb_insert", CB_INSERT.isChecked());
		editor.putBoolean("p_cb_update", CB_UPDATE.isChecked());
		editor.putBoolean("p_cb_delete", CB_DELETE.isChecked());


		//Toast.makeText(this, "store : cb count "+ set.get_cb_count() , Toast.LENGTH_SHORT).show();
		//print_values();
		editor.commit();
	}


	public void set_default() {


		editor.putInt("p_target_partition", 0);
		set.set_target_partition(0);
		editor.putInt("p_threadnum", 1);
		set.set_thread_num(1);			
		editor.putInt("p_filesize_w", 10);
		set.set_filesize_write(10);
		editor.putInt("p_filesize_r", 32);
		set.set_filesize_read(32);		
		editor.putInt("p_io_size", 0);
		set.set_io_size(0);
		editor.putInt("p_file_sync_mode", 3);
		set.set_file_sync_mode(0);		
		editor.putInt("p_transaction", 100);
		set.set_transaction_num(100);	
		editor.putInt("p_sql_sync_mode", 2);
		set.set_sql_sync_mode(1);
		editor.putInt("p_journal_mode", 1);
		set.set_journal_mode(1);		

		editor.putInt("p_cb_count",0);
		set.set_cb_count(0);

		/* Checkbox */
		set.set_seq_write(false);
		set.set_seq_read(false);
		set.set_ran_write(false);
		set.set_ran_read(false);
		set.set_insert(false);
		set.set_update(false);
		set.set_delete(false);


		editor.putBoolean("p_cb_sw", false);
		editor.putBoolean("p_cb_sr", false);
		editor.putBoolean("p_cb_rw",false);
		editor.putBoolean("p_cb_rr",false);
		editor.putBoolean("p_cb_insert",false);
		editor.putBoolean("p_cb_update",false);
		editor.putBoolean("p_cb_delete",false);

		editor.commit();			
	}


	/* For Debug */
	public void print_values() {
		Toast.makeText(this, "target_partition "+ set.get_target_partition(), Toast.LENGTH_SHORT).show();
		Toast.makeText(this, "thread_num "+ set.get_thread_num(), Toast.LENGTH_SHORT).show();	
		Toast.makeText(this, "filesize_write "+ set.get_filesize_write(), Toast.LENGTH_SHORT).show();
		Toast.makeText(this, "filesize_read "+ set.get_filesize_read(), Toast.LENGTH_SHORT).show();

		Toast.makeText(this, "recode_size "+ set.get_io_size(), Toast.LENGTH_SHORT).show();

		Toast.makeText(this, "file_sync_mode "+ set.get_file_sync_mode(), Toast.LENGTH_SHORT).show();		

		Toast.makeText(this, "transaction_num "+ set.get_transaction_num(), Toast.LENGTH_SHORT).show();
		Toast.makeText(this, "journal_mode "+ set.get_journal_mode(), Toast.LENGTH_SHORT).show();
		Toast.makeText(this, "sql_sync_mode "+ set.get_sql_sync_mode(), Toast.LENGTH_SHORT).show();

	}
	public boolean onCreateOptionsMenu(Menu menu){
		MenuInflater inflater = getMenuInflater();
		inflater.inflate(R.menu.menu, menu);

		return true;
	}

	public boolean onOptionsItemSelected(MenuItem item){
		Intent intent;
		switch(item.getItemId()){

		case R.id.menu_info:
			intent = new Intent(TabMain.this, Info.class);
			startActivity(intent);
			return true;
		}
		return false;
	}

	public void print_error(int type)
	{ 
		switch(type){
		case 0:
			Toast.makeText(this, "MobiBench working..", Toast.LENGTH_SHORT).show();
			break;
		case 1:
			Toast.makeText(this, "Benchmark engin exited with error", Toast.LENGTH_LONG).show();
			break;
		case 2:
			Toast.makeText(this, "The file size must be less than the free space.", Toast.LENGTH_SHORT).show();
			break;
		}

	}

	public void print_exp(int flag){
		switch(flag){
		case 0:
			Toast.makeText(this, "Start Benchmark : File, SQlite", Toast.LENGTH_SHORT).show();
			break;
		case 1:
			Toast.makeText(this, "Start Benchmark : File", Toast.LENGTH_SHORT).show();
			break;
		case 2:
			Toast.makeText(this, "Start Benchmark : SQlite", Toast.LENGTH_SHORT).show();
			break;
		case 3:
			Toast.makeText(this, "Start Benchmark : Customized set", Toast.LENGTH_SHORT).show();
			break;
		case 4:
			Toast.makeText(this, "Nothing selected. Check \"Setting tab\"", Toast.LENGTH_SHORT).show();
			break;

		}

	}
	protected void onResume(){
		super.onResume();
		db_index = prefs.getInt("database_index", 0); // data base indexing
		arr.clear();
		for(int i=0; i<db_index; i++){
			result = dbAdapter.fetchNote(i);// 횟수 제한
			result.moveToFirst();
			db_date = " " + result.getString(2) + "  ( " + result.getString(1) + " )";
			arr.add(db_date);
			//aa.notifyDataSetChanged();
		}

		ListView list = (ListView) findViewById(R.id.ListView01);  
		aa = new ArrayAdapter<String> (       // ListView에 할당할 ArrayAdapter 생성
				this, R.layout.history_listitem, arr);                   // 여기에 앞에서 만든 ArrayList를 사용한다
		list.setAdapter(aa);                                                              // ListView에 ArrayAdapter 설정

	}
	
	public String getDeviceId() 
	{ 
		TelephonyManager mgr = (TelephonyManager)getSystemService(Context.TELEPHONY_SERVICE); 
		
		if( mgr.getDeviceId() == null){
			Build bd = new Build();
			final String serialNum = bd.SERIAL;
			//Toast.makeText(this, "Build serial " + serialNum, Toast.LENGTH_SHORT).show();
			return serialNum;		
		}else{
			//Toast.makeText(this, "Device Id " + mgr.getDeviceId(), Toast.LENGTH_SHORT).show();
			return mgr.getDeviceId();	
		}
	} 

	public static String getMD5Hash(String s) {  
		MessageDigest m = null;  
		String hash = null;  

		try {  
			m = MessageDigest.getInstance("MD5");  
			m.update(s.getBytes(),0,s.length());  
			hash = new BigInteger(1, m.digest()).toString(16);  
		} catch (NoSuchAlgorithmException e) {  
			e.printStackTrace();  
		}  

		return hash;  
	}  

	public void set_configuration(){

		DialogActivity.g_partition=sp_partition.getSelectedItem().toString();
		DialogActivity.g_thread = et_threadnum.getText().toString();
		DialogActivity.g_file_size_w = et_filesize_w.getText().toString();
		DialogActivity.g_file_size_r = et_filesize_r.getText().toString();
		DialogActivity.g_io_size = sp_io_size.getSelectedItem().toString();
		DialogActivity.g_file_mode = sp_file_sync.getSelectedItem().toString();
		DialogActivity.g_transaction_mode= et_transaction.getText().toString();
		DialogActivity.g_sqlite_mode = sp_sql_sync.getSelectedItem().toString();
		DialogActivity.g_sqlite_journal = sp_journal.getSelectedItem().toString();

		/*
		Log.d(DEBUG_TAG, "[JWGOM] Configuration value " + DialogActivity.g_partition);
		Log.d(DEBUG_TAG, "[JWGOM] Configuration value " + DialogActivity.g_thread);
		Log.d(DEBUG_TAG, "[JWGOM] Configuration value " + DialogActivity.g_file_size_w);
		Log.d(DEBUG_TAG, "[JWGOM] Configuration value " + DialogActivity.g_file_size_r);
		Log.d(DEBUG_TAG, "[JWGOM] Configuration value " + DialogActivity.g_io_size);
		Log.d(DEBUG_TAG, "[JWGOM] Configuration value " + DialogActivity.g_file_mode);
		Log.d(DEBUG_TAG, "[JWGOM] Configuration value " + DialogActivity.g_transaction_mode);
		Log.d(DEBUG_TAG, "[JWGOM] Configuration value " + DialogActivity.g_sqlite_mode);
		Log.d(DEBUG_TAG, "[JWGOM] Configuration value " + DialogActivity.g_sqlite_journal);
		 */
		return ;
	}
	
	public String check_default(){
		String tmp_result = "1";
		
		if( !(sp_partition.getSelectedItemPosition() == 0)) tmp_result = "0";
			//Log.d(DEBUG_TAG, "[JWGOM] ------------sp_partition " + (sp_partition.getSelectedItemPosition() == 0) + "[" + tmp_result + "]");
		if(!(et_threadnum.getText().toString().equals("1"))) tmp_result = "0";
		//	Log.d(DEBUG_TAG, "[JWGOM] ------------et_threadnum " + et_threadnum.getText().toString().equals("1") + "[" + tmp_result + "]");
		if(!(et_filesize_w.getText().toString().equals("10"))) tmp_result = "0";
		//	Log.d(DEBUG_TAG, "[JWGOM] -----------et_filesize_w " + et_filesize_w.getText().toString().equals("10") + "[" + tmp_result + "]");
		if(!(et_filesize_r.getText().toString().equals("32"))) tmp_result = "0";
		//	Log.d(DEBUG_TAG, "[JWGOM] -----------et_filesize_r " + et_filesize_r.getText().toString().equals("32") + "[" + tmp_result + "]");
		
		if(!(sp_io_size.getSelectedItemPosition() == 0)) tmp_result = "0";
			//Log.d(DEBUG_TAG, "[JWGOM] --------------sp_io_size " + (sp_io_size.getSelectedItemPosition() == 0) + "[" + tmp_result + "]");
		if(!(sp_file_sync.getSelectedItemPosition() == 3)) tmp_result = "0";
		//	Log.d(DEBUG_TAG, "[JWGOM] ------------sp_file_sync " + (sp_file_sync.getSelectedItemPosition() == 3) + "[" + tmp_result + "]");
		
		if(!(et_transaction.getText().toString().equals("100"))) tmp_result = "0";
		//	Log.d(DEBUG_TAG, "[JWGOM] ----------et_transaction " + et_transaction.getText().toString().equals("100") + "[" + tmp_result + "]");
		
		if(!(sp_sql_sync.getSelectedItemPosition() == 2)) tmp_result = "0";
		//	Log.d(DEBUG_TAG, "[JWGOM] -------------sp_sql_sync " + (sp_sql_sync.getSelectedItemPosition() == 2) + "[" + tmp_result + "]");
		if(!(sp_journal.getSelectedItemPosition() == 1)) tmp_result = "0";		
		//	Log.d(DEBUG_TAG, "[JWGOM] -------------sp_journal " + (sp_journal.getSelectedItemPosition() == 1) + "[" + tmp_result + "]");
		
		
		
		
		
		
		
		//Log.d(DEBUG_TAG, "[JWGOM] ------------ Return value is : " +tmp_result);
		return tmp_result;
	}
	
	class StartAni implements Runnable {
	    public void run() {
	      	anidrawable.start();  
	    }
	    public void stop(){
	    	
	    }
	}

}
