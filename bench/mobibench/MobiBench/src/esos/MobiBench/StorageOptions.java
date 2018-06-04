package esos.MobiBench;

import java.io.File;
import java.util.ArrayList;
import java.util.Scanner;

import android.os.Environment;
import android.os.StatFs;

public class StorageOptions {
	private static ArrayList<String> mMounts = new ArrayList<String>();
	private static ArrayList<String> mVold = new ArrayList<String>();

	public static String[] labels;
	public static String[] paths;
	public static int count = 0;
	public static boolean b_2nd_sdcard = false;

	public static String determineStorageOptions() {
		readMountsFile();

		readVoldFile();

		compareMountsWithVold();

		testAndCleanMountsList();

		//setProperties();
		System.out.println("mobibench secondary sdcard path final: "+ mMounts.toString() );

		String ret = null;
		if(mMounts.size() > 0) {
			b_2nd_sdcard = true;
			ret = mMounts.get(0);
			mMounts.clear();
		}

		return ret;
	}


	public static long getAvailableSize(String path) {

		System.out.println("Dir  : "+ path);

		StatFs stat = new StatFs(path);   

		long block_size = stat.getBlockSize();
		long blocks = stat.getAvailableBlocks();		
		long free_size = block_size * blocks;

		return free_size;
	}

	public static String formatSize(long size) {
		String suffix = null;

		if (size >= 1024) {
			suffix = "KB";
			size /= 1024;
			if (size >= 1024) {
				suffix = "MB";
				size /= 1024;
				if (size >= 1024) {
					suffix = "GB";
					size /= 1024;
				}
			}
		}

		StringBuilder resultBuffer = new StringBuilder(Long.toString(size));
		int commaOffset = resultBuffer.length() - 3;
		while (commaOffset > 0) {
			resultBuffer.insert(commaOffset, ',');
			commaOffset -= 3;
		}
		if (suffix != null)
			resultBuffer.append(suffix);

		return resultBuffer.toString();

	}

	public static String GetFileSystemName() {
		String ret_str = "unknown";

		try {
			Scanner scanner = new Scanner(new File("/proc/mounts"));
			while (scanner.hasNext()) {
				String line = scanner.nextLine();
		         String[] lineElements = line.split(" ");
		         if (lineElements[1].contentEquals("/data")) {
		           ret_str = lineElements[2];
		           break;
		         }
			}
		} catch (Exception e) {
			// Auto-generated catch block
			e.printStackTrace();
		}

		System.out.println("Filesystem Name: "+ ret_str);

		return ret_str;
	}

	private static void readMountsFile() {
		/*
		 * Scan the /proc/mounts file and look for lines like this:
		 * /dev/block/vold/179:1 /mnt/sdcard vfat rw,dirsync,nosuid,nodev,noexec,relatime,uid=1000,gid=1015,fmask=0602,dmask=0602,allow_utime=0020,codepage=cp437,iocharset=iso8859-1,shortname=mixed,utf8,errors=remount-ro 0 0
		 * 
		 * When one is found, split it into its elements
		 * and then pull out the path to the that mount point
		 * and add it to the arraylist
		 */

		// some mount files don't list the default
		// path first, so we add it here to
		// ensure that it is first in our list
		//  mMounts.add("/mnt/sdcard");

		try {
			Scanner scanner = new Scanner(new File("/proc/mounts"));
			while (scanner.hasNext()) {
				String line = scanner.nextLine();
				if (line.startsWith("/dev/block/vold/")) {
					String[] lineElements = line.split(" ");
					String element = lineElements[1];

					// don't add the default mount path
					// it's already in the list.
					if (!element.equals(Environment.getExternalStorageDirectory().getPath())) {
						mMounts.add(element);
						//System.out.println("mobibench secondary sdcard path mount: "+ element);
					}
				}
			}
		} catch (Exception e) {
			// Auto-generated catch block
			e.printStackTrace();
		}
	}

	private static void readVoldFile() {
		/*
		 * Scan the /system/etc/vold.fstab file and look for lines like this:
		 * dev_mount sdcard /mnt/sdcard 1 /devices/platform/s3c-sdhci.0/mmc_host/mmc0
		 * 
		 * When one is found, split it into its elements
		 * and then pull out the path to the that mount point
		 * and add it to the arraylist
		 */

		// some devices are missing the vold file entirely
		// so we add a path here to make sure the list always
		// includes the path to the first sdcard, whether real
		// or emulated.
		// mVold.add("/mnt/sdcard");

		try {
			Scanner scanner = new Scanner(new File("/system/etc/vold.fstab"));
			while (scanner.hasNext()) {
				String line = scanner.nextLine();
				if (line.startsWith("dev_mount")) {
					String[] lineElements = line.split(" ");
					String element = lineElements[2];

					if (element.contains(":"))
						element = element.substring(0, element.indexOf(":"));

					// don't add the default vold path
					// it's already in the list.
					if (!element.equals(Environment.getExternalStorageDirectory().getPath())) {
						mVold.add(element);
						//System.out.println("mobibench secondary sdcard path vold: "+ element);
					}
				}
			}
		} catch (Exception e) {
			// Auto-generated catch block
			e.printStackTrace();
		}
	}

	private static void compareMountsWithVold() {
		/*
		 * Sometimes the two lists of mount points will be different.
		 * We only want those mount points that are in both list.
		 * 
		 * Compare the two lists together and remove items that are not in both lists.
		 */

		for (int i = 0; i < mMounts.size(); i++) {
			String mount = mMounts.get(i);
			if (!mVold.contains(mount)) mMounts.remove(i--);
		}

		// don't need this anymore, clear the vold list to reduce memory
		// use and to prepare it for the next time it's needed.
		mVold.clear();
	}

	private static void testAndCleanMountsList() {
		/*
		 * Now that we have a cleaned list of mount paths
		 * Test each one to make sure it's a valid and
		 * available path. If it is not, remove it from
		 * the list. 
		 */

		for (int i = 0; i < mMounts.size(); i++) {
			String mount = mMounts.get(i);
			File root = new File(mount);
			if (!root.exists() || !root.isDirectory() || !root.canWrite())
				mMounts.remove(i--);
		}
	}

}

