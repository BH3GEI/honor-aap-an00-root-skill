package ai.mofa.honorautoroot;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.Switch;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;

public final class MainActivity extends Activity {
    private TextView status;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        int padding = (int) (20 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        status = new TextView(this);
        status.setTextSize(15);
        layout.addView(status, new LinearLayout.LayoutParams(-1, -2));

        Button check = new Button(this);
        check.setText("Check root (no exploit)");
        check.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { startRootService(RootService.ACTION_CHECK); }
        });
        layout.addView(check);

        Button run = new Button(this);
        run.setText("Run once if root is absent");
        run.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { startRootService(RootService.ACTION_RUN); }
        });
        layout.addView(run);

        Switch auto = new Switch(this);
        auto.setText("Restore temporary root after boot (default off)");
        auto.setChecked(getSharedPreferences("settings", MODE_PRIVATE)
                .getBoolean("auto_root", false));
        auto.setOnCheckedChangeListener(new android.widget.CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(android.widget.CompoundButton view, boolean enabled) {
                getSharedPreferences("settings", MODE_PRIVATE).edit()
                        .putBoolean("auto_root", enabled).apply();
            }
        });
        layout.addView(auto);
        setContentView(layout);
        showStatus();
        if (getIntent() != null && getIntent().getBooleanExtra("run_root", false)) {
            startRootService(RootService.ACTION_RUN);
        } else {
            startRootService(RootService.ACTION_CHECK);
        }
    }

    private void startRootService(String action) {
        startForegroundService(new Intent(this, RootService.class).setAction(action));
        status.postDelayed(new Runnable() {
            @Override public void run() { showStatus(); }
        }, 1200);
    }

    private void showStatus() {
        String text = "Honor AutoRoot\napp uid=" + Process.myUid()
                + "\nNo partition writes. Existing root is never re-exploited.\n\n";
        try (FileInputStream input = new FileInputStream(new File(getFilesDir(), "status.txt"))) {
            byte[] bytes = new byte[8192];
            int n = input.read(bytes);
            text += new String(bytes, 0, Math.max(0, n), StandardCharsets.UTF_8);
        } catch (Exception e) {
            text += "No status yet";
        }
        status.setText(text);
    }

    @Override protected void onResume() {
        super.onResume();
        showStatus();
    }
}
