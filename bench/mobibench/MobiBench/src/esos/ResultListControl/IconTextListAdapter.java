package esos.ResultListControl;


import java.util.ArrayList;
import java.util.List;
import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;

public class IconTextListAdapter extends BaseAdapter {

	private Context mContext;

	private List<IconTextItem> mItems = new ArrayList<IconTextItem>();

	public IconTextListAdapter(Context context) {
		mContext = context;
	}

	public void addItem(IconTextItem it) {
		mItems.add(it);
	}

	public void setListItems(List<IconTextItem> lit) {
		mItems = lit;
	}

	public int getCount() {
		return mItems.size();
	}

	public Object getItem(int position) {
		return mItems.get(position);
	}

	public boolean areAllItemsSelectable() {
		return false;
	}

	public boolean isSelectable(int position) {
		try {
			return mItems.get(position).isSelectable();
		} catch (IndexOutOfBoundsException ex) {
			return false;
		}
	}

	public long getItemId(int position) {
		return position;
	}

	public View getView(int position, View convertView, ViewGroup parent) {
		IconLastView itemLastView;


		if (convertView == null) {
			itemLastView = new IconLastView(mContext, mItems.get(position));
		} else {
			itemLastView = (IconLastView) convertView;           
			itemLastView.setIcon(mItems.get(position).getIcon());
			itemLastView.setText(0, mItems.get(position).getData(0));
			itemLastView.setText(1, mItems.get(position).getData(1));
			itemLastView.setText(2, mItems.get(position).getData(2));
			itemLastView.setText(3, mItems.get(position).getData(3));
			itemLastView.setText(4, mItems.get(position).getData(4));
			itemLastView.setText(5, mItems.get(position).getData(5));
			itemLastView.setText(6, mItems.get(position).getData(6));

		}

		return itemLastView;	

	}

}