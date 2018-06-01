package esos.MobiBench;

import esos.MobiBench.R;
import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

public class MobiBench extends Activity implements Runnable{
    /** Called when the activity is first created. */
    
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);
        CDialog.showLoading(this);
        (new Thread(this)).start();
    }
    
    public void run(){
    	try{
    		Thread.sleep(3000); // delay time configuration
    	}catch(Exception e){}
    	CDialog.hideLoading();
    	
        Intent intent= new Intent(this, TabMain.class);        
        startActivity(intent);
        
        finish();
    }

}