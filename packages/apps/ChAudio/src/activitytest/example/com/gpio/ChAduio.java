package activitytest.example.com.gpio;

import android.util.Log;

import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;

public class ChAduio {
    public boolean enable(){
		String string = "1";
		byte[] bytes = string.getBytes();
		try {
			OutputStream out = new FileOutputStream("/sys/devices/virtual/ch_audio/gpio_audio/gpio_audio");
			out.write(bytes, 0, 1);
            Log.e("GetGpio", "GetGpio my success.");
		}
		catch(IOException e) {
			Log.e("GetGpio", "GetGpio Fail." + e);
		}
		return true;
    }
    public boolean disable(){
        String command = String.format("echo 0 > /sys/devices/virtual/ch_audio/gpio_audio/gpio_audio");
        try {
            String[] test = new String[] {"/system/bin/sh", "-c", command};
            Runtime.getRuntime().exec(test);
            Log.e("GetGpio", "GetGpio my success.");
            return true;
        } catch (IOException e) {
            Log.e("GetGpio", "GetGpio fail.");
            return false;
        }
    }
}
