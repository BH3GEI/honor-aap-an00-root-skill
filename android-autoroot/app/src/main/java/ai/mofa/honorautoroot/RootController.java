package ai.mofa.honorautoroot;

import android.content.Context;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

final class RootController {
    private RootController() {}

    static boolean isRootAvailable() {
        Log.i("HonorAutoRoot", "checking root availability");
        Process process = null;
        try {
            process = new ProcessBuilder("/data/local/tmp/su", "-c", "id")
                    .redirectErrorStream(true).start();
            if (!process.waitFor(8, TimeUnit.SECONDS)) {
                process.destroyForcibly();
                return false;
            }
            String output = readAll(process.getInputStream());
            Log.i("HonorAutoRoot", "su -c id rc=" + process.exitValue() + " out=" + output.trim());
            return process.exitValue() == 0 && output.contains("uid=0");
        } catch (Exception e) {
            return false;
        } finally {
            if (process != null) process.destroy();
        }
    }

    static String runRoot(Context context) {
        String binary = context.getApplicationInfo().nativeLibraryDir + "/libhonorroot.so";
        File executable = new File(binary);
        if (!executable.isFile() || !executable.canExecute()) {
            return "native executable missing: " + binary;
        }
        Log.i("HonorAutoRoot", "running native exploit: " + binary);
        File log = new File(context.getFilesDir(), "root-run.log");
        Process process = null;
        try {
            process = new ProcessBuilder(binary, "--run")
                    .redirectErrorStream(true)
                    .redirectOutput(log)
                    .start();
            if (!process.waitFor(240, TimeUnit.SECONDS)) {
                process.destroyForcibly();
                return "exploit timed out; no retry was scheduled\n" + readTail(log);
            }
            int rc = process.exitValue();
            Log.i("HonorAutoRoot", "native rc=" + rc + " root=" + isRootAvailable());
            if (rc == 0 && isRootAvailable()) {
                return "temporary root is active";
            }
            return "exploit did not produce a usable root session (rc=" + rc + ")\n" + readTail(log);
        } catch (Exception e) {
            return "failed to start native executable: " + e;
        } finally {
            if (process != null) process.destroy();
        }
    }

    private static String readAll(InputStream input) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        int n;
        while ((n = input.read(buffer)) >= 0) output.write(buffer, 0, n);
        return output.toString(StandardCharsets.UTF_8.name());
    }

    private static String readTail(File file) {
        try (FileInputStream input = new FileInputStream(file)) {
            long skip = Math.max(0, file.length() - 4000);
            if (skip > 0) input.skip(skip);
            return readAll(input);
        } catch (Exception e) {
            return "no log available";
        }
    }
}
