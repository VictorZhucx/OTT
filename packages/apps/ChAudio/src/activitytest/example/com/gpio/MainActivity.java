package activitytest.example.com.gpio;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;

public class MainActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        Button button2 = (Button)findViewById(R.id.button2);
        button2.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                ChAduio chAduio = new ChAduio();
                chAduio.enable();
            }
        });
        Button button3 = (Button)findViewById(R.id.button3);
        button3.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Log.e("chadio", "on click");
                ChAduio chAduio = new ChAduio();
                chAduio.disable();
            }
        });
        Button button4 = (Button)findViewById(R.id.button4);
        button4.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Log.e("chadio", "on click====");
                GpioAduio gpio = new GpioAduio();
                gpio.enableSpk();
            }
        });
        Button button5 = (Button)findViewById(R.id.button5);
        button5.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Log.e("chadio", "on clickxxx");
                GpioAduio gpio = new GpioAduio();
                gpio.disableSpk();
            }
        });
    }
}
