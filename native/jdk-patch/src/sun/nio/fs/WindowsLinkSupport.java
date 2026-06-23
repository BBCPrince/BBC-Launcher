package sun.nio.fs;

import java.io.IOException;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;

/**
 * Xbox UWP child-process replacement for sun.nio.fs.WindowsLinkSupport.
 *
 * When the launcher (a UWP app) spawns java.exe on Xbox dev mode, the child process gets
 * an AppContainer-flavored token: FILE_GENERIC_READ works against package LocalState, but
 * FILE_READ_ATTRIBUTES is denied. The original WindowsLinkSupport.getRealPath opens the
 * path with FILE_FLAG_BACKUP_SEMANTICS | FILE_READ_ATTRIBUTES via CreateFileW and that call
 * fails with AccessDeniedException - which propagates as Path.toRealPath() failing for
 * client.jar, game/downloads, and game/crash-reports, crashing Minecraft during init.
 *
 * Minecraft has no symlinks or NTFS reparse points in its tree, and the launcher fully
 * controls the file layout. We replace WindowsLinkSupport with a version that does not
 * open any file handle: the returned "real path" is the input's normalized absolute path,
 * which is exactly what callers like Mojang's bfp.c and ZipFileSystemProvider need.
 *
 * Activated by:  java.exe --patch-module java.base=xbox-jdk-patch.jar
 * Java 21+ builds use xbox-jdk-link-patch.jar, which contains only this class.
 */
class WindowsLinkSupport {

    private WindowsLinkSupport() {
    }

    static String getRealPath(WindowsPath input, boolean resolveLinks) throws IOException {
        return resolve(input);
    }

    static String getFinalPath(WindowsPath input, boolean followLinks) throws IOException {
        return resolve(input);
    }

    static String getFinalPath(WindowsPath input) throws IOException {
        return resolve(input);
    }

    static String readLink(WindowsPath input) throws IOException {
        throw new NoSuchFileException(input.toString());
    }

    private static String resolve(WindowsPath input) {
        if (input == null) {
            return null;
        }
        Path abs = input.toAbsolutePath().normalize();
        String s = abs.toString();
        // Strip the Win32 long-path prefix that some normalization paths add - callers
        // (especially zipfs) compare with the input which never has the prefix.
        if (s.startsWith("\\\\?\\")) {
            s = s.substring(4);
        }
        return s;
    }
}
