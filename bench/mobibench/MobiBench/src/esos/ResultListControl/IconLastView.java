package esos.ResultListControl;

import esos.ResultListControl.DialogActivity;
import esos.MobiBench.R;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.Intent;
import android.graphics.drawable.Drawable;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

@SuppressLint("ViewConstructor")
public class IconLastView extends LinearLayout {

	/**
	 * Icon
	 */
	private ImageView mIcon;

	/**
	 * TextView 01
	 */
	private TextView mText01r;
	private TextView mText02r;
	private TextView mText03r;
	private TextView mText04r;
	private TextView mText05r;
	private TextView mText06r;
	private TextView mText07r; // experiment
	private String pos = null;
	// private TextView line;


	private ImageButton btn_webview;
	private String clip_str;

	Context this_context;

	public IconLastView(Context context, IconTextItem aItem) {
		super(context);

		this_context = context;
		// Layout Inflation
		LayoutInflater inflater = (LayoutInflater) context.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
		inflater.inflate(R.layout.lastlistitem, this, true);

		// Set Icon
		mIcon = (ImageView) findViewById(R.id.iconItemr);
		mIcon.setImageDrawable(aItem.getIcon());

		//  line = (TextView) findViewById(R.id.list_last_line);

		// Set Text 01
		mText01r = (TextView) findViewById(R.id.dataItem01r);
		mText01r.setText(aItem.getData(0));

		// Set Text 02
		mText02r = (TextView) findViewById(R.id.dataItem02r);
		mText02r.setText(aItem.getData(1));

		// Set Text 03
		mText03r = (TextView) findViewById(R.id.dataItem03r);
		mText03r.setText(aItem.getData(2));

		// Set Text 04
		mText04r = (TextView) findViewById(R.id.dataItem04r);
		mText04r.setText(aItem.getData(3));

		// Set Text 05
		mText05r = (TextView) findViewById(R.id.dataItem05r);
		mText05r.setText(aItem.getData(4));

		// Set Text 06
		mText06r = (TextView) findViewById(R.id.dataItem06r);
		mText06r.setText(aItem.getData(5));

		// Set Text 06
		mText07r = (TextView) findViewById(R.id.dataItem07r);
		mText07r.setText(aItem.getData(6));

		pos = aItem.getData(6);
		btn_webview = (ImageButton)findViewById(R.id.ibtn_webview);
		findViewById(R.id.ibtn_webview).setOnClickListener(mClickListener);

	}


	Button.OnClickListener mClickListener = new View.OnClickListener() {
		public void onClick(View v) {
			switch(v.getId()){
			case R.id.ibtn_webview:

				if (DialogActivity.isWifiConn || DialogActivity.isMobileConn) {
					Intent intent = new Intent(this_context, Webview.class);
					DialogActivity.G_EXP_CHOICE = pos;            	
					this_context.startActivity(intent);	
				}else{
					Toast.makeText(this_context, "Not connected to wifi or 3g/4g", Toast.LENGTH_SHORT).show();
				}

				break;
			}
		}
	};


	/**
	 * set Text
	 *
	 * @param index
	 * @param data
	 */
	public void setText(int index, String data) {
		if (index == 0) {
			mText01r.setText(data);
		} else if (index == 1) {
			mText02r.setText(data);
		} else if (index == 2) {
			mText03r.setText(data);
		} else if (index == 3) {
			mText04r.setText(data);
		} else if (index == 4) {
			mText05r.setText(data);
		} else if (index == 5) {
			mText06r.setText(data);
		} else if (index == 6) {
			mText07r.setText(data);
		} else {
			throw new IllegalArgumentException();
		}
	}


	public void setClip(String clip){
		clip_str = clip;
	}


	/**
	 * set Icon
	 *
	 * @param icon
	 */
	public void setIcon(Drawable icon) {
		mIcon.setImageDrawable(icon);
	}

}