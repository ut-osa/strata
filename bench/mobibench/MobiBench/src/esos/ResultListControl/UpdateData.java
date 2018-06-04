package esos.ResultListControl;

import java.io.*;
import java.net.*;
import java.util.Scanner;

import android.app.*;
import android.os.*;
import android.util.Log;


public class UpdateData extends Activity {
	private static final String DEBUG="net_access";
	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		Log.d(DEBUG, "create updatadata ");

		//HttpPostData("10","10","10","10","10","10","10");
		Log.d(DEBUG, "fin");
	}

	public void HttpPostData(String seq_w, String seq_r, String ran_w, String ran_r, String sq_in
			, String sq_up, String sq_del, String sn, String c_partition, String c_thread, String c_file_size_w
			, String c_file_size_r, String c_io_size, String c_file_mode, String c_tran, String c_sqlite_mode
			, String c_sqlite_journal, String c_filesystem, String def) {
		try {
			URL url = new URL(
					"http://mobibench.dothome.co.kr/insert_data.php"); // URL
			HttpURLConnection http = (HttpURLConnection) url.openConnection(); // 접속

			http.setDefaultUseCaches(false);
			http.setDoInput(true);
			http.setDoOutput(true);
			http.setRequestMethod("POST");


			http.setRequestProperty("content-type","application/x-www-form-urlencoded");

			StringBuffer buffer = new StringBuffer();
			String model = Build.MODEL;
			String android_ver = "Android " + Build.VERSION.RELEASE;
			String kernel_ver = System.getProperty("os.name") + System.getProperty("os.version");

			//total memory
			String totalmem = "unknown";
			Scanner m_scanner = new Scanner(new File("/proc/meminfo"));
			while (m_scanner.hasNext()) {
				String line = m_scanner.nextLine();
				String[] lineElements = line.replaceAll("\\p{Space}","").split(":");
				
				if (lineElements[0].contentEquals("MemTotal")) {
					totalmem = lineElements[1];
					break;
		         }
			}

			//eMMC chip number
			String emmc_num = "unknown";
			Scanner e_scanner = new Scanner(new File("/sys/class/block/mmcblk0/device/cid"));
			emmc_num = e_scanner.nextLine();

			buffer.append("model").append("=").append(model).append("&");
			buffer.append("android_ver").append("=").append(android_ver).append("&");

			buffer.append("seq_w").append("=").append(seq_w).append("&");
			buffer.append("seq_r").append("=").append(seq_r).append("&");
			buffer.append("ran_w").append("=").append(ran_w).append("&");
			buffer.append("ran_r").append("=").append(ran_r).append("&");
			buffer.append("sq_in").append("=").append(sq_in).append("&");
			buffer.append("sq_up").append("=").append(sq_up).append("&");
			buffer.append("sq_del").append("=").append(sq_del).append("&");
			buffer.append("sn").append("=").append(sn).append("&"); //임시

			buffer.append("c_partition").append("=").append(c_partition).append("&");
			buffer.append("c_thread").append("=").append(c_thread).append("&");
			buffer.append("c_file_size_w").append("=").append(c_file_size_w).append("&");
			buffer.append("c_file_size_r").append("=").append(c_file_size_r).append("&");
			buffer.append("c_io_size").append("=").append(c_io_size).append("&");
			buffer.append("c_file_mode").append("=").append(c_file_mode).append("&");
			buffer.append("c_tran").append("=").append(c_tran).append("&");
			buffer.append("c_sqlite_mode").append("=").append(c_sqlite_mode).append("&");
			buffer.append("c_sqlite_journal").append("=").append(c_sqlite_journal).append("&");
			buffer.append("c_filesystem").append("=").append(c_filesystem).append("&");
			buffer.append("def").append("=").append(def).append("&");
			buffer.append("kernel_ver").append("=").append(kernel_ver).append("&");
			buffer.append("totalmem").append("=").append(totalmem).append("&");
			buffer.append("emmc_num").append("=").append(emmc_num);



			OutputStreamWriter outStream = new OutputStreamWriter(
					http.getOutputStream(), "EUC-KR");
			PrintWriter writer = new PrintWriter(outStream);
			writer.write(buffer.toString());

			writer.flush();

			// --------------------------
			// 서버에서 전송받기
			// --------------------------
			InputStreamReader tmp = new InputStreamReader(
					http.getInputStream(), "EUC-KR");
			BufferedReader reader = new BufferedReader(tmp);
			StringBuilder builder = new StringBuilder();
			String str;
			while ((str = reader.readLine()) != null) { // 서버에서 라인단위로 보내줄 것이므로
				//Log.d(DEBUG, "[url] str : " + str);	// 라인단위로 읽는다
				builder.append(str + "\n"); // View에 표시하기 위해 라인 구분자 추가
			}



		} catch (MalformedURLException e) {
			//
		} catch (IOException e) {
			//
		} // try
	} // HttpPostData
} // Activity