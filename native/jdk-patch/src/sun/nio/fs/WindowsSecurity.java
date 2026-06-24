package sun.nio.fs;

/**
 * Xbox UWP child-process replacement for sun.nio.fs.WindowsSecurity.
 *
 * OpenJDK's Windows access checks ask the process token whether paths allow
 * generic write access. On Xbox dev-mode AppContainer processes this can report
 * false for LocalState files even when normal file writes succeed. ZipFS uses
 * Files.isWritable() during startup and marks remap output jars read-only when
 * that token check lies, which breaks Fabric remapping on first launch.
 *
 * Actual file opens still enforce write access later. This patch only makes the
 * advisory access-mask check agree with the working LocalState write path.
 */
class WindowsSecurity {
    static final long processTokenWithDuplicateAccess = 0L;
    static final long processTokenWithQueryAccess = 0L;

    private static final Privilege NOOP_PRIVILEGE = new Privilege() {
        @Override
        public void drop() {
        }
    };

    private WindowsSecurity() {
    }

    private static long openProcessToken(int desiredAccess) {
        return 0L;
    }

    static Privilege enablePrivilege(String privilege) {
        return NOOP_PRIVILEGE;
    }

    static boolean checkAccessMask(
            long securityDescriptor,
            int access,
            int genericRead,
            int genericWrite,
            int genericExecute,
            int genericAll) throws WindowsException {
        return true;
    }

    interface Privilege {
        void drop();
    }
}
