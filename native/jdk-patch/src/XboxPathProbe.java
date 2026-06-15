// Xbox UWP child-process path probe (precompiled to .class so the JRE doesn't need jdk.compiler).
// Runs as: java.exe -cp <native-dir> XboxPathProbe <path1> <path2> ...
// For each path it tests createDirectories, getFileStore, toRealPath, write+delete.
// Output: one PROBE| line per path that the launcher parses.

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AccessDeniedException;
import java.nio.file.DirectoryStream;
import java.nio.file.FileStore;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.Paths;

public class XboxPathProbe {

    public static void main(String[] args) {
        System.out.println("PROBE_BEGIN|java.version=" + System.getProperty("java.version"));
        System.out.println("PROBE_BEGIN|os.name=" + System.getProperty("os.name"));
        System.out.println("PROBE_BEGIN|user.dir=" + System.getProperty("user.dir"));
        System.out.println("PROBE_BEGIN|java.io.tmpdir=" + System.getProperty("java.io.tmpdir"));

        if (args.length == 0) {
            System.out.println("PROBE_ERROR|no candidate paths supplied");
            return;
        }

        for (String raw : args) {
            String path = raw == null ? "" : raw.trim();
            StringBuilder line = new StringBuilder("PROBE|").append(path);

            Path p;
            try {
                p = Paths.get(path);
            } catch (Throwable t) {
                line.append("|invalid=").append(shortMsg(t));
                System.out.println(line);
                continue;
            }

            line.append("|createDir=").append(runStep(new CreateDirsStep(p)));
            line.append("|getFileStore=").append(runStep(new GetFileStoreStep(p)));
            line.append("|toRealPath=").append(runStep(new ToRealPathStep(p, true)));
            line.append("|toRealPath_links=").append(runStep(new ToRealPathStep(p, false)));
            line.append("|write=").append(runStep(new WriteStep(p)));
            line.append("|listDir=").append(runStep(new ListDirStep(p)));

            System.out.println(line);
        }

        System.out.println("PROBE_END");
    }

    private static String runStep(ProbeStep step) {
        try {
            step.run();
            return "OK";
        } catch (AccessDeniedException e) {
            return "FAIL_ACCESS:" + shortMsg(e);
        } catch (Throwable t) {
            return "FAIL:" + shortMsg(t);
        }
    }

    private static String shortMsg(Throwable t) {
        String name = t.getClass().getSimpleName();
        String message = t.getMessage();
        if (message == null) {
            return name;
        }
        message = message.replace('|', '/').replace('\n', ' ').replace('\r', ' ');
        if (message.length() > 160) {
            message = message.substring(0, 160) + "...";
        }
        return name + ":" + message;
    }

    private interface ProbeStep {
        void run() throws Exception;
    }

    private static final class CreateDirsStep implements ProbeStep {
        private final Path p;
        CreateDirsStep(Path p) { this.p = p; }
        public void run() throws Exception { Files.createDirectories(p); }
    }

    private static final class GetFileStoreStep implements ProbeStep {
        private final Path p;
        GetFileStoreStep(Path p) { this.p = p; }
        public void run() throws Exception { FileStore ignored = Files.getFileStore(p); }
    }

    private static final class ToRealPathStep implements ProbeStep {
        private final Path p;
        private final boolean noLinks;
        ToRealPathStep(Path p, boolean noLinks) { this.p = p; this.noLinks = noLinks; }
        public void run() throws Exception {
            if (noLinks) {
                p.toRealPath(LinkOption.NOFOLLOW_LINKS);
            } else {
                p.toRealPath();
            }
        }
    }

    private static final class WriteStep implements ProbeStep {
        private final Path dir;
        WriteStep(Path dir) { this.dir = dir; }
        public void run() throws Exception {
            Path probe = dir.resolve(".xbox-path-probe-" + System.nanoTime() + ".tmp");
            Files.write(probe, "ok".getBytes(StandardCharsets.UTF_8));
            try { Files.deleteIfExists(probe); } catch (Throwable ignored) { }
        }
    }

    private static final class ListDirStep implements ProbeStep {
        private final Path dir;
        ListDirStep(Path dir) { this.dir = dir; }
        public void run() throws Exception {
            DirectoryStream<Path> stream = Files.newDirectoryStream(dir);
            try {
                int count = 0;
                for (Path ignored : stream) {
                    if (++count >= 1) {
                        break;
                    }
                }
            } finally {
                stream.close();
            }
        }
    }
}
