package activitytest.example.com.gpio;

public class GpioAduio {
    static  {
        System.loadLibrary("jni_gpioaduio");
    }
    //enable speaker
    public native static void enableSpk();

    //disable speaker
    public native static void disableSpk();
}