package esos.ResultListControl;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.ListView;



public class DataListView extends ListView {
	/**
	 * DataAdapter for this instance
	 */
	private IconTextListAdapter adapter;


	/**
	 * Listener for data selection
	 */
	private OnDataSelectionListener selectionListener;

	public DataListView(Context context) {
		super(context);
		init();
	}

	public DataListView(Context context, AttributeSet attrs) {
		super(context, attrs);
		init();
	}

	// jwgom add
	public void setBackgroundResource(int resid){
		super.setBackgroundResource(resid);
	}

	public void setCacheColorHint(int color){
		super.setCacheColorHint(color);
	}

	/**
	 * set initial properties
	 */
	private void init() {
		// set OnItemClickListener for processing OnDataSelectionListener
		setOnItemClickListener(new OnItemClickAdapter());
	}

	/**
	 * set DataAdapter
	 * 
	 * @param adapter
	 */
	public void setAdapter(BaseAdapter adapter) {
		super.setAdapter(adapter);

	}

	/**
	 * get DataAdapter
	 * 
	 * @return
	 */
	public BaseAdapter getAdapter() {
		return (BaseAdapter)super.getAdapter();
	}

	/**
	 * set OnDataSelectionListener
	 * 
	 * @param listener
	 */
	public void setOnDataSelectionListener(OnDataSelectionListener listener) {
		this.selectionListener = listener;
	}

	/**
	 * get OnDataSelectionListener
	 * 
	 * @return
	 */
	public OnDataSelectionListener getOnDataSelectionListener() {
		return selectionListener;
	}

	class OnItemClickAdapter implements OnItemClickListener {

		public OnItemClickAdapter() {

		}

		public void onItemClick(AdapterView parent, View v, int position, long id) {

			if (selectionListener == null) {
				return;
			}

			// call the OnDataSelectionListener method
			selectionListener.onDataSelected(parent, v, position, id);




		}

	}



}