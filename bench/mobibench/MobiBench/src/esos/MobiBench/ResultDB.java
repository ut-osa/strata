package esos.MobiBench;

import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;
import android.util.Log;

public class ResultDB {

	/**
	 * TAG for debugging
	 */
	public static final String TAG = "CustomerDatabase";

	/**
	 * Singleton instance
	 */
	private static ResultDB database;


	/**
	 * database name
	 */
	public static String DATABASE_NAME = "customer.db";

	/**
	 * table name for CUSTOMER_INFO
	 */
	public static String TABLE_CUSTOMER_INFO = "CUSTOMER_INFO";

	/**
	 * table name for MEMO
	 */
	public static String TABLE_MEMO = "MEMO";

	/**
	 * version
	 */
	public static int DATABASE_VERSION = 1;


	/**
	 * Helper class defined
	 */
	private DatabaseHelper dbHelper;

	/**
	 * Database object
	 */
	private SQLiteDatabase db;


	private Context context;

	/**
	 * Constructor
	 */
	private ResultDB(Context context) {
		this.context = context;
	}


	public static ResultDB getInstance(Context context) {
		if (database == null) {
			database = new ResultDB(context);
		}

		return database;
	}

	/**
	 * open database
	 *
	 * @return
	 */
	public boolean open() {
		println("opening database [" + DATABASE_NAME + "].");

		dbHelper = new DatabaseHelper(context);
		db = dbHelper.getWritableDatabase();

		return true;
	}

	/**
	 * close database
	 */
	public void close() {
		println("closing database [" + DATABASE_NAME + "].");
		db.close();
		database = null;
	}

	/**
	 * execute raw query using the input SQL
	 * close the cursor after fetching any result
	 *
	 * @param SQL
	 * @return
	 */
	public Cursor rawQuery(String SQL) {
		println("\nexecuteQuery called.\n");

		Cursor c1 = null;
		try {
			c1 = db.rawQuery(SQL, null);
			println("cursor count : " + c1.getCount());
		} catch(Exception ex) {
			Log.e(TAG, "Exception in executeQuery", ex);
		}

		return c1;
	}

	public boolean execSQL(String SQL) {
		println("\nexecute called.\n");

		try {
			Log.d(TAG, "SQL : " + SQL);
			db.execSQL(SQL);
		} catch(Exception ex) {
			Log.e(TAG, "Exception in executeQuery", ex);
			return false;
		}

		return true;
	}




	private class DatabaseHelper extends SQLiteOpenHelper
	{
		public DatabaseHelper(Context context)
		{
			super(context, DATABASE_NAME, null, DATABASE_VERSION);
		}

		public void onCreate(SQLiteDatabase db)
		{
			// TABLE_CUSTOMER_INFO
			println("creating table [" + TABLE_CUSTOMER_INFO + "].");

			// drop existing table
			String DROP_SQL = "drop table if exists " + TABLE_CUSTOMER_INFO;
			try {
				db.execSQL(DROP_SQL);
			} catch(Exception ex) {
				Log.e(TAG, "Exception in DROP_SQL", ex);
			}

			// create table
			String CREATE_SQL = "create table " + TABLE_CUSTOMER_INFO + "("
					+ "  _id INTEGER  NOT NULL PRIMARY KEY AUTOINCREMENT, "
					+ "  NAME TEXT, "
					+ "  AGE INTEGER, "
					+ "  MOBILE TEXT, "
					+ "  CREATE_DATE TIMESTAMP DEFAULT CURRENT_TIMESTAMP "
					+ ")";
			try {
				db.execSQL(CREATE_SQL);
			} catch(Exception ex) {
				Log.e(TAG, "Exception in CREATE_SQL", ex);
			}


			// TABLE_MEMO
			println("creating table [" + TABLE_MEMO + "].");

			// drop existing table
			DROP_SQL = "drop table if exists " + TABLE_MEMO;
			try {
				db.execSQL(DROP_SQL);
			} catch(Exception ex) {
				Log.e(TAG, "Exception in DROP_SQL", ex);
			}

			// create table
			CREATE_SQL = "create table " + TABLE_MEMO + "("
					+ "  _id INTEGER  NOT NULL PRIMARY KEY AUTOINCREMENT, "
					+ "  CUSTOMER_ID INTEGER, "
					+ "  CONTENTS TEXT, "
					+ "  TITLE TEXT, "
					+ "  CREATE_DATE TIMESTAMP DEFAULT CURRENT_TIMESTAMP "
					+ ")";
			try {
				db.execSQL(CREATE_SQL);
			} catch(Exception ex) {
				Log.e(TAG, "Exception in CREATE_SQL", ex);
			}

		}

		public void onOpen(SQLiteDatabase db)
		{
			println("opened database [" + DATABASE_NAME + "].");

		}

		public void onUpgrade(SQLiteDatabase db, int oldVersion,
				int newVersion)
		{
			println("Upgrading database from version " + oldVersion + " to " + newVersion + ".");

			if (oldVersion < 2) {   // version 1

			}

		}

	}

	private void println(String msg) {
		Log.d(TAG, msg);
	}


}