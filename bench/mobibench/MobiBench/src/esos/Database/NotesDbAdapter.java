package esos.Database;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.SQLException;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;
import android.util.Log;


public class NotesDbAdapter {


	public static final String KEY_ID = "_id";
	public static final String KEY_DATE = "date";
	public static final String KEY_TYPE = "type";

	public static final String KEY_HAS_RESULT = "has_result";
	public static final String KEY_ACT = "act";
	public static final String KEY_IO = "io";
	public static final String KEY_IDL = "idl";
	public static final String KEY_CT_TOTAL = "ct_total";
	public static final String KEY_CT_VOL = "ct_vol";
	public static final String KEY_THRP = "thrp";
	public static final String KEY_EXP_NAME ="exp_name";

	private static final String TAG = "MobiDB";

	private DatabaseHelper mDbHelper;
	private SQLiteDatabase mDb;

	/**
	 * Database creation sql statement
	 */
	private static final String DATABASE_CREATE = "create table mobidb " + "(" 
			+ "_id integer, "
			+ "date text, "
			+ "type text, "
			+ "has_result integer, "
			+ "act text, "
			+ "io text, "
			+ "idl text, "
			+ "ct_total text, "
			+ "ct_vol text, "
			+ "thrp text, "
			+ "exp_name text not null);";


	private static final String DATABASE_NAME = "MobiDB";
	private static final String DATABASE_TABLE = "mobidb";
	private static final int DATABASE_VERSION = 1;

	private final Context mCtx;

	private static class DatabaseHelper extends SQLiteOpenHelper {

		DatabaseHelper(Context context) {
			super(context, DATABASE_NAME, null, DATABASE_VERSION);
		}

		@Override
		public void onCreate(SQLiteDatabase db) {

			db.execSQL(DATABASE_CREATE);
		}

		@Override
		public void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion) {
			Log.w(TAG, "Upgrading database from version " + oldVersion + " to "
					+ newVersion + ", which will destroy all old data");
			db.execSQL("DROP TABLE IF EXISTS notes");
			onCreate(db);
		}
	}

	public NotesDbAdapter(Context ctx) {
		this.mCtx = ctx;
	}

	public NotesDbAdapter open() throws SQLException {
		mDbHelper = new DatabaseHelper(mCtx);
		mDb = mDbHelper.getWritableDatabase();
		return this;
	}

	public void close() {
		mDbHelper.close();
	}

	public long insert_DB(int _id, String date, String type, int has_result, String act, 
			String io, String idl, String ct_total, String ct_vol, String thrp, String exp_name) {
		ContentValues initialValues = new ContentValues();
		initialValues.put(KEY_ID, _id);
		initialValues.put(KEY_DATE, date);
		initialValues.put(KEY_TYPE, type);
		initialValues.put(KEY_HAS_RESULT, has_result);
		initialValues.put(KEY_ACT, act);
		initialValues.put(KEY_IO, io);
		initialValues.put(KEY_IDL, idl);
		initialValues.put(KEY_CT_TOTAL, ct_total);
		initialValues.put(KEY_CT_VOL, ct_vol);
		initialValues.put(KEY_THRP, thrp);
		initialValues.put(KEY_EXP_NAME, exp_name);

		return mDb.insert(DATABASE_TABLE, null, initialValues);
	}

	public Cursor selectTable(String index){
		return mDb.rawQuery("SELECT * FROM mobidb WHERE _id = index", null);
	}
	/*
    public boolean deleteNote(long rowId) {

        Log.i("Delete called", "value__" + rowId);
        return mDb.delete(DATABASE_TABLE, KEY_ROWID + "=" + rowId, null) > 0;
    }
	 */
	public Cursor fetchAllNotes() {

		return mDb.query(DATABASE_TABLE, new String[] { KEY_ID,KEY_DATE,KEY_TYPE, KEY_HAS_RESULT, KEY_ACT, KEY_IO, KEY_IDL, KEY_CT_TOTAL, KEY_CT_VOL, KEY_THRP, KEY_EXP_NAME  }, null, null, null, null, null);
	}

	public Cursor fetchNote(long rowId) throws SQLException {

		Cursor mCursor =   
				mDb.query(true, DATABASE_TABLE, new String[] 
						{KEY_ID,KEY_DATE,KEY_TYPE, KEY_HAS_RESULT, KEY_ACT, KEY_IO, KEY_IDL, KEY_CT_TOTAL, KEY_CT_VOL, KEY_THRP, KEY_EXP_NAME },
						KEY_ID + "=" + rowId, null, null, null, null, null);
		if (mCursor != null) {
			mCursor.moveToFirst();
		}
		return mCursor;

	}
	/*
    public boolean updateNote(long rowId, String title, String body) {
        ContentValues args = new ContentValues();
        args.put(KEY_TITLE, title);
        args.put(KEY_BODY, body);

        return mDb.update(DATABASE_TABLE, args, KEY_ROWID + "=" + rowId, null) > 0;
    }*/
}