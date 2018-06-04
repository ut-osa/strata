package esos.ResultListControl;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import esos.MobiBench.R;
import esos.Database.*;
import android.annotation.TargetApi;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.Button;
import android.widget.Toast;
import android.view.View;
import esos.MobiBench.StorageOptions;

@TargetApi(11)
public class DialogActivity extends Activity {

	DataListView list;
	IconTextListAdapter adapter;

	public static String clip_text = null;
	public static String clip_title = null;

	private SharedPreferences db_prefs = null;
	private SharedPreferences.Editor pref_editor = null;   
	private static int db_index = 0;
	private Calendar calendar = Calendar.getInstance();
	private SimpleDateFormat dateFormat = new SimpleDateFormat("yyyyMMdd_HH_mm_ss");
	private SimpleDateFormat dateFormat_file = new SimpleDateFormat("yyyyMMdd_HHmm");
	private static final String TAG_DA="datedebug";
	private static NotesDbAdapter db;
	public static int bHasResult[] = new int[7];
	private static final String DEBUG_TAG="dialogactivity";

	private Bitmap bm;
	private UpdateData u_data = null;

	public static void ClearResult(NotesDbAdapter database) {
		db = database;
		for(int i=0; i < 7; i++) {
			bHasResult[i]=0;
			ResultCPU_act[i]=null;
			ResultCPU_iow[i]=null;
			ResultCPU_idl[i]=null;
			ResultCS_tot[i]=null;
			ResultCS_vol[i]=null;
			ResultThrp[i]=null;
			ResultExpName[i]=null;
			ResultType[i] = null;
		}
	}


	public static int index_db;
	public static String ResultCPU_act[] = new String[7];
	public static String ResultCPU_iow[] = new String[7];
	public static String ResultCPU_idl[] = new String[7];
	public static String ResultCS_tot[] = new String[7];
	public static String ResultCS_vol[] = new String[7];
	public static String ResultThrp[] = new String[7];
	public static String ResultExpName[] = new String[7];
	public static String ResultType[] = new String[7];
	public static String ResultDate;

	/* Global value for save : testing configuration */
	public static String g_partition = null;
	public static String g_thread = null;
	public static String g_file_size_w=null;
	public static String g_file_size_r=null;
	public static String g_io_size=null;
	public static String g_file_mode=null;
	public static String g_transaction_mode=null;
	public static String g_sqlite_mode=null;
	public static String g_sqlite_journal=null;  
	public static String g_def = null;
	public static String G_EXP_CHOICE = "default_g_exp_choice";
	
	public static int check_using_db = 0;
	public static boolean isWifiAvail = false;
	public static boolean isWifiConn = false;
	public static boolean isMobileAvail = false;
	public static boolean isMobileConn = false;
	public static String dev_num = null;

	public void onCreate(Bundle savedInstanceState) {

		super.onCreate(savedInstanceState);
		requestWindowFeature(Window.FEATURE_CUSTOM_TITLE);


		db_prefs = getSharedPreferences("Setting", MODE_PRIVATE);

		pref_editor = db_prefs.edit();

		// window feature for no title - must be set prior to calling setContentView.
		//requestWindowFeature(Window.FEATURE_NO_TITLE);

		// create a DataGridView instance
		ViewGroup.LayoutParams params = new ViewGroup.LayoutParams(ViewGroup.LayoutParams.FILL_PARENT, ViewGroup.LayoutParams.FILL_PARENT);
		list = new DataListView(this);


		// create an DataAdapter and a MTable
		adapter = new IconTextListAdapter(this);

		// add items
		Resources res = getResources();

		int resID[] = new int[7];
		resID[0]=R.drawable.icon_sw;
		resID[1]=R.drawable.icon_sr;
		resID[2]=R.drawable.icon_rw;
		resID[3]=R.drawable.icon_rr;
		resID[4]=R.drawable.icon_insert;
		resID[5]=R.drawable.icon_update;
		resID[6]=R.drawable.icon_delete;

		if(ResultDate == null) { // ResultDate가 0인 경우는 새로운 실험을 시작했을 때만.
			ResultDate = dateFormat.format(calendar.getTime()); // for data base date
		}
		db_index = db_prefs.getInt("database_index", 0); // data base indexing
		clip_text = clip_title = "";

		clip_text = "* " + Build.MANUFACTURER.toUpperCase()+" "+Build.MODEL+"\n* " + ResultDate + "\n\n";
		clip_title = dateFormat_file.format(calendar.getTime()); ;
		for(int idx = 0; idx < 7; idx++) {
			if(bHasResult[idx] != 0) {
				adapter.addItem(new IconTextItem(res.getDrawable(resID[idx]), ResultCPU_act[idx], ResultCPU_iow[idx]
						, ResultCPU_idl[idx] ,ResultCS_tot[idx], ResultCS_vol[idx], ResultThrp[idx], ResultExpName[idx]));

				clip_text += "- " + ResultExpName[idx]+": "+ResultThrp[idx]+"\n"+" ▪ CPU: "+ResultCPU_act[idx]+","
						+ ResultCPU_iow[idx]+","+ResultCPU_idl[idx]+"\n"+" ▪ CTX_SW: "+ResultCS_tot[idx]
								+ "("+ResultCS_vol[idx]+")\n\n";
				if(check_using_db == 1){
					//Log.d(DEBUG_TAG, "addItem / checkusing is 1 : idx/expname " + idx + " " + ResultExpName[idx]);	
					db.insert_DB(db_index, ResultDate, ResultType[idx], 1, ResultCPU_act[idx], ResultCPU_iow[idx]
							, ResultCPU_idl[idx], ResultCS_tot[idx], ResultCS_vol[idx],  ResultThrp[idx], ResultExpName[idx]);
				}
			}
		}
		if(check_using_db == 1){
			db_index++;
			pref_editor.putInt("database_index", db_index);
			pref_editor.commit();
		}

		list.setAdapter(adapter);

		setContentView(list, params);


		getWindow().setFeatureInt(Window.FEATURE_CUSTOM_TITLE, R.layout.custome_title);

		findViewById(R.id.ibtn_share).setOnClickListener(myButtonClick);
		findViewById(R.id.ibtn_save).setOnClickListener(myButtonClick);

		ConnectivityManager cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
		NetworkInfo ni = cm.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
		isWifiAvail = ni.isAvailable();
		isWifiConn = ni.isConnected();
		ni = cm.getNetworkInfo(ConnectivityManager.TYPE_MOBILE);
		isMobileAvail = ni.isAvailable();
		isMobileConn = ni.isConnected();	        

		if(check_using_db == 1){
			if (isWifiConn || isMobileConn) {

				AlertDialog alert =  new AlertDialog.Builder(this)
				.setTitle("Send Results")
				.setMessage("Submit the performance result to the ranking server for research purposes." 
						+ "\n" + "(No personally identifiable information is collected.)")
						.setCancelable(true)
						.setPositiveButton("Accept", new DialogInterface.OnClickListener() {
							public void onClick(DialogInterface dialog, int whichButton) {	          
								u_data = new UpdateData();
								String tmp_string[] = new String[7];
								for(int k=0; k<7; k++){
									if(bHasResult[k] == 1){
										tmp_string[k] = ResultThrp[k];
									}else{
										tmp_string[k] = "-1";
									}
								}
								u_data.HttpPostData(tmp_string[0],tmp_string[1],tmp_string[2],tmp_string[3]
										,tmp_string[4],tmp_string[5],tmp_string[6], dev_num
										, g_partition.substring(1), g_thread, g_file_size_w, g_file_size_r, g_io_size, g_file_mode
										,g_transaction_mode, g_sqlite_mode, g_sqlite_journal, StorageOptions.GetFileSystemName(),g_def);
								Log.d(DEBUG_TAG, "DEFAULT : " + g_def);
								Toast.makeText(DialogActivity.this, "send result to server", Toast.LENGTH_SHORT).show();
							}
						})
						.setNegativeButton("Decline", new DialogInterface.OnClickListener() {
							public void onClick(DialogInterface dialog, int whichButton) {

							}
						})
						.show();	  
			}   
		}	       	

	}



	Button.OnClickListener myButtonClick = new Button.OnClickListener(){
		public void onClick(View v){
			switch(v.getId()){
			case R.id.ibtn_share:
				try {					
					screenshot();

				} catch (Exception e1) {
					// TODO Auto-generated catch block
					e1.printStackTrace();
				}


				break;

			case R.id.ibtn_save:
				String txtFilename = DialogActivity.ResultDate + ".txt";
				String txtPath;
				String ext = Environment.getExternalStorageState();
				if (ext.equals(Environment.MEDIA_MOUNTED)) {
					txtPath = Environment.getExternalStorageDirectory().getAbsolutePath()+"/";
				} else {
					txtPath = Environment.MEDIA_UNMOUNTED+"/";
				}
				File txtDir = new File(txtPath + "/MobiBench/"); 
				if (!txtDir.exists()) {
					txtPath = "";
					txtDir.mkdirs();
				}


				String tmp = DialogActivity.clip_text;
				// 파일 생성
				try{
					File txtFile = new File(Environment.getExternalStorageDirectory() + "/MobiBench/", txtFilename);
					txtFile.createNewFile();

					BufferedWriter out = new BufferedWriter(new FileWriter(txtFile));
					out.write(tmp);
					out.newLine();
					out.close();
				}catch(IOException e){}

				String TotalPath = txtPath+"MobiBench/" + txtFilename;
				Toast.makeText(getApplicationContext(), "File saved : " + TotalPath, 3000).show();
				break;

			}			
		}
	};


	public void screenshot() throws Exception {   

		View view = this.getWindow().getDecorView(); // 전체 화면의 view를 가져온다
		view.setDrawingCacheEnabled(true);
		Bitmap screenshot = view.getDrawingCache();


		String filename = DialogActivity.ResultDate + ".jpg";
		String Path;
		String ext = Environment.getExternalStorageState();
		if (ext.equals(Environment.MEDIA_MOUNTED)) {
			Path = Environment.getExternalStorageDirectory().getAbsolutePath()+"/";
		} else {
			Path = Environment.MEDIA_UNMOUNTED+"/";
		}
		File dir = new File(Path + "/MobiBenchImage/"); 
		if (!dir.exists()) {
			Path = "";
			dir.mkdirs();
		}

		try {
			File f = new File(Environment.getExternalStorageDirectory() + "/MobiBenchImage/", filename);
			f.createNewFile();
			OutputStream outStream = new FileOutputStream(f);
			screenshot.compress(Bitmap.CompressFormat.PNG, 100, outStream);
			outStream.close();
		}catch (IOException e) {
			e.printStackTrace();
		}
		view.setDrawingCacheEnabled(false);

		String sTotalPath = Environment.getExternalStorageDirectory() + "/MobiBenchImage/"+ filename;

		Uri uri = Uri.fromFile(new File(sTotalPath));
		Intent shareIntent = new Intent();
		shareIntent.setAction(Intent.ACTION_SEND);
		shareIntent.putExtra(Intent.EXTRA_STREAM, uri);
		shareIntent.setType("image/jpeg");
		startActivity(Intent.createChooser(shareIntent, "공유하기"));


	}

	public boolean onKeyDown(int keyCode, KeyEvent event){
		if(keyCode == KeyEvent.KEYCODE_BACK){
			ResultDate = null;
		}
		return super.onKeyDown(keyCode, event);
	}   
}


