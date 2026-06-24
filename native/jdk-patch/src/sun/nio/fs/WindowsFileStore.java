package sun.nio.fs;

import java.io.File;
import java.io.IOException;
import java.nio.file.FileStore;
import java.nio.file.attribute.BasicFileAttributeView;
import java.nio.file.attribute.DosFileAttributeView;
import java.nio.file.attribute.FileAttributeView;
import java.nio.file.attribute.FileStoreAttributeView;

/**
 * Xbox UWP child-process replacement for sun.nio.fs.WindowsFileStore.
 *
 * Some Xbox AppContainer LocalState paths report the volume as read-only through
 * the JDK's Windows file-store probe even though CreateFile/WriteFile succeeds.
 * Files.isWritable() treats a read-only FileStore as a hard no, and ZipFS then
 * opens Fabric remap jars as read-only. This shim keeps the FileStore writable
 * and leaves actual file access enforcement to the real file opens.
 */
class WindowsFileStore extends FileStore {
    private final String root;
    private final String displayName;

    private WindowsFileStore(String root) {
        this.root = root == null || root.isEmpty() ? "." : root;
        this.displayName = this.root;
    }

    static WindowsFileStore create(String root, boolean ignoreNotReady) throws IOException {
        return new WindowsFileStore(root);
    }

    static WindowsFileStore create(WindowsPath path) throws IOException {
        return new WindowsFileStore(path == null ? "." : path.toString());
    }

    WindowsNativeDispatcher.VolumeInformation volumeInformation() {
        return null;
    }

    int volumeType() {
        return 3;
    }

    @Override
    public String name() {
        return displayName;
    }

    @Override
    public String type() {
        return "LocalState";
    }

    @Override
    public boolean isReadOnly() {
        return false;
    }

    @Override
    public long getTotalSpace() throws IOException {
        return new File(root).getTotalSpace();
    }

    @Override
    public long getUsableSpace() throws IOException {
        return new File(root).getUsableSpace();
    }

    @Override
    public long getBlockSize() throws IOException {
        return 4096L;
    }

    @Override
    public long getUnallocatedSpace() throws IOException {
        return new File(root).getFreeSpace();
    }

    @Override
    public <V extends FileStoreAttributeView> V getFileStoreAttributeView(Class<V> type) {
        return null;
    }

    @Override
    public Object getAttribute(String attribute) throws IOException {
        if ("totalSpace".equals(attribute)) {
            return Long.valueOf(getTotalSpace());
        }
        if ("usableSpace".equals(attribute) || "unallocatedSpace".equals(attribute)) {
            return Long.valueOf(getUsableSpace());
        }
        if ("blockSize".equals(attribute) || "bytesPerSector".equals(attribute)) {
            return Long.valueOf(getBlockSize());
        }
        if ("volume:isRemovable".equals(attribute) || "volume:isCdrom".equals(attribute)) {
            return Boolean.FALSE;
        }
        if ("volume:vsn".equals(attribute)) {
            return Integer.valueOf(0);
        }
        throw new UnsupportedOperationException("'" + attribute + "' not recognized");
    }

    @Override
    public boolean supportsFileAttributeView(Class<? extends FileAttributeView> type) {
        return type == BasicFileAttributeView.class || type == DosFileAttributeView.class;
    }

    @Override
    public boolean supportsFileAttributeView(String name) {
        return "basic".equals(name) || "dos".equals(name);
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof WindowsFileStore && root.equals(((WindowsFileStore)other).root);
    }

    @Override
    public int hashCode() {
        return root.hashCode();
    }

    @Override
    public String toString() {
        return displayName;
    }
}
