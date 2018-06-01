package esos.ResultListControl;


import android.view.View;
import android.widget.AdapterView;

/**
* Interface that is called when an item is selected in DataListView
* 
* @author Mike
*/
public interface OnDataSelectionListener {

   /**
    * Method that is called when an item is selected in DataListView
    * 
    * @param parent Parent View
    * @param v Target View
    * @param row Row Index
    * @param column Column Index
    * @param id ID for the View
    */
   public void onDataSelected(AdapterView parent, View v, int position, long id);
    
}