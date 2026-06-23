package com.minecraftxbox.lan;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.Instrumentation;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.MulticastSocket;
import java.net.NetworkInterface;
import java.security.ProtectionDomain;
import java.util.Enumeration;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

public final class ModernLanAgent {
    private static final String DISCOVERY_PATCH_PROPERTY = "minecraft.xbox.lan.discoveryPatch";
    private static final String COBBLEMON_SHOWDOWN_FS_PATCH_PROPERTY = "minecraft.xbox.cobblemon.showdownFsPatch";
    private static final String ZIPFS_WRITABLE_PATCH_PROPERTY = "minecraft.xbox.zipfsWritablePatch";
    private static final String LAN_DETECTOR_INTERMEDIARY = "net/minecraft/class_1134$class_1135";
    private static final String LAN_DETECTOR_OBFUSCATED = "iqc$a";
    private static final String COBBLEMON_SHOWDOWN_FILE_SYSTEM =
            "com/cobblemon/mod/common/battles/runner/graal/GraalShowdownService$createContext$1";
    private static final String ZIP_FILE_SYSTEM = "jdk/nio/zipfs/ZipFileSystem";
    private static volatile boolean lanPatchEnabled;
    private static volatile boolean cobblemonShowdownFsPatchEnabled;
    private static volatile boolean zipFsWritablePatchEnabled;
    private static volatile boolean loggedSocketReady;

    private ModernLanAgent() {
    }

    public static void premain(String agentArgs, Instrumentation instrumentation) {
        lanPatchEnabled = Boolean.getBoolean(DISCOVERY_PATCH_PROPERTY);
        cobblemonShowdownFsPatchEnabled = Boolean.getBoolean(COBBLEMON_SHOWDOWN_FS_PATCH_PROPERTY);
        zipFsWritablePatchEnabled = Boolean.getBoolean(ZIPFS_WRITABLE_PATCH_PROPERTY);
        if (!lanPatchEnabled && !cobblemonShowdownFsPatchEnabled && !zipFsWritablePatchEnabled) {
            return;
        }

        instrumentation.addTransformer(new Transformer(), false);
        log("modern patch agent installed lanPatch=" + lanPatchEnabled
                + " cobblemonShowdownFsPatch=" + cobblemonShowdownFsPatchEnabled
                + " zipFsWritablePatch=" + zipFsWritablePatchEnabled);
    }

    public static void configureLanDetectorSocket(MulticastSocket socket) {
        if (socket == null) {
            return;
        }

        try {
            socket.setReuseAddress(true);
            Inet4Address address = findLocalIpv4Address();
            if (address == null) {
                log("LAN detector socket could not find a non-loopback IPv4 interface");
                return;
            }

            // Minecraft uses joinGroup(InetAddress) here, so set the legacy
            // IPv4 interface before it joins 224.0.2.60.
            socket.setInterface(address);
            if (!loggedSocketReady) {
                loggedSocketReady = true;
                NetworkInterface networkInterface = NetworkInterface.getByInetAddress(address);
                log("LAN detector socket using IPv4 " + address.getHostAddress()
                        + (networkInterface == null ? "" : " on " + networkInterface.getName()));
            }
        } catch (Throwable ex) {
            log("LAN detector socket patch failed: " + ex);
        }
    }

    private static Inet4Address findLocalIpv4Address() {
        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            while (interfaces != null && interfaces.hasMoreElements()) {
                NetworkInterface networkInterface = interfaces.nextElement();
                if (!networkInterface.isUp() || networkInterface.isLoopback()) {
                    continue;
                }

                Enumeration<InetAddress> addresses = networkInterface.getInetAddresses();
                while (addresses.hasMoreElements()) {
                    InetAddress address = addresses.nextElement();
                    if (address instanceof Inet4Address
                            && !address.isLoopbackAddress()
                            && !address.isAnyLocalAddress()) {
                        return (Inet4Address)address;
                    }
                }
            }
        } catch (Throwable ex) {
            log("LAN detector IPv4 enumeration failed: " + ex);
        }

        return null;
    }

    private static synchronized void log(String message) {
        String line = "[Xbox-LAN] " + message;
        System.err.println(line);

        String userHome = System.getProperty("user.home", ".");
        File logDir = new File(userHome, "logs");
        if (!logDir.exists()) {
            logDir.mkdirs();
        }

        File logFile = new File(logDir, "xbox-lan-agent.log");
        try (FileWriter writer = new FileWriter(logFile, true)) {
            writer.write(line);
            writer.write(System.lineSeparator());
        } catch (IOException ignored) {
        }
    }

    private static boolean isLanDetector(String className) {
        return LAN_DETECTOR_INTERMEDIARY.equals(className) || LAN_DETECTOR_OBFUSCATED.equals(className);
    }

    private static boolean isCobblemonShowdownFileSystem(String className) {
        return COBBLEMON_SHOWDOWN_FILE_SYSTEM.equals(className);
    }

    private static boolean isZipFileSystem(String className) {
        return ZIP_FILE_SYSTEM.equals(className);
    }

    private static final class Transformer implements ClassFileTransformer {
        @Override
        public byte[] transform(
                ClassLoader loader,
                String className,
                Class<?> classBeingRedefined,
                ProtectionDomain protectionDomain,
                byte[] classfileBuffer) {
            boolean patchLanDetector = lanPatchEnabled && isLanDetector(className);
            boolean patchCobblemonShowdownFs =
                    cobblemonShowdownFsPatchEnabled && isCobblemonShowdownFileSystem(className);
            boolean patchZipFsWritable = zipFsWritablePatchEnabled && isZipFileSystem(className);
            if (!patchLanDetector && !patchCobblemonShowdownFs && !patchZipFsWritable) {
                return null;
            }

            try {
                ClassReader reader = new ClassReader(classfileBuffer);
                ClassWriter writer = new ClassWriter(reader, ClassWriter.COMPUTE_MAXS);
                ClassVisitor visitor = writer;
                if (patchLanDetector) {
                    visitor = new LanDetectorVisitor(visitor, className);
                }
                if (patchCobblemonShowdownFs) {
                    visitor = new CobblemonShowdownFileSystemVisitor(visitor);
                }
                if (patchZipFsWritable) {
                    visitor = new ZipFileSystemWritableVisitor(visitor);
                }
                reader.accept(visitor, 0);
                if (patchLanDetector) {
                    log("patched LAN detector " + className);
                }
                if (patchCobblemonShowdownFs) {
                    log("patched Cobblemon Showdown file-system guard");
                }
                if (patchZipFsWritable) {
                    log("patched ZipFileSystem writable checks");
                }
                return writer.toByteArray();
            } catch (Throwable ex) {
                log("class patch failed for " + className + ": " + ex);
                return null;
            }
        }
    }

    private static final class LanDetectorVisitor extends ClassVisitor {
        private final String owner;

        LanDetectorVisitor(ClassVisitor visitor, String owner) {
            super(Opcodes.ASM9, visitor);
            this.owner = owner;
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            MethodVisitor visitor = super.visitMethod(access, name, descriptor, signature, exceptions);
            if ("<init>".equals(name)) {
                return new ConstructorVisitor(visitor, owner);
            }
            return visitor;
        }
    }

    private static final class ConstructorVisitor extends MethodVisitor {
        private final String owner;
        private boolean injected;

        ConstructorVisitor(MethodVisitor visitor, String owner) {
            super(Opcodes.ASM9, visitor);
            this.owner = owner;
        }

        @Override
        public void visitFieldInsn(int opcode, String fieldOwner, String name, String descriptor) {
            super.visitFieldInsn(opcode, fieldOwner, name, descriptor);
            if (!injected
                    && opcode == Opcodes.PUTFIELD
                    && owner.equals(fieldOwner)
                    && "Ljava/net/MulticastSocket;".equals(descriptor)) {
                injected = true;
                super.visitVarInsn(Opcodes.ALOAD, 0);
                super.visitFieldInsn(Opcodes.GETFIELD, fieldOwner, name, descriptor);
                super.visitMethodInsn(
                        Opcodes.INVOKESTATIC,
                        "com/minecraftxbox/lan/ModernLanAgent",
                        "configureLanDetectorSocket",
                        "(Ljava/net/MulticastSocket;)V",
                        false);
            }
        }
    }

    private static final class CobblemonShowdownFileSystemVisitor extends ClassVisitor {
        CobblemonShowdownFileSystemVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM9, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            if ("checkAccess".equals(name)
                    && "(Ljava/nio/file/Path;Ljava/util/Set;[Ljava/nio/file/LinkOption;)V".equals(descriptor)) {
                MethodVisitor visitor = super.visitMethod(access, name, descriptor, signature, exceptions);
                visitor.visitCode();
                visitor.visitInsn(Opcodes.RETURN);
                visitor.visitMaxs(0, 0);
                visitor.visitEnd();
                return null;
            }
            return super.visitMethod(access, name, descriptor, signature, exceptions);
        }
    }

    private static final class ZipFileSystemWritableVisitor extends ClassVisitor {
        ZipFileSystemWritableVisitor(ClassVisitor visitor) {
            super(Opcodes.ASM9, visitor);
        }

        @Override
        public MethodVisitor visitMethod(
                int access,
                String name,
                String descriptor,
                String signature,
                String[] exceptions) {
            if ("isReadOnly".equals(name) && "()Z".equals(descriptor)) {
                MethodVisitor visitor = super.visitMethod(access, name, descriptor, signature, exceptions);
                visitor.visitCode();
                visitor.visitInsn(Opcodes.ICONST_0);
                visitor.visitInsn(Opcodes.IRETURN);
                visitor.visitMaxs(1, 1);
                visitor.visitEnd();
                return null;
            }

            if ("checkWritable".equals(name) && "()V".equals(descriptor)) {
                MethodVisitor visitor = super.visitMethod(access, name, descriptor, signature, exceptions);
                visitor.visitCode();
                visitor.visitInsn(Opcodes.RETURN);
                visitor.visitMaxs(0, 1);
                visitor.visitEnd();
                return null;
            }

            if (name.startsWith("lambda$new$")
                    && "(Ljava/nio/file/Path;)Ljava/lang/Boolean;".equals(descriptor)) {
                MethodVisitor visitor = super.visitMethod(access, name, descriptor, signature, exceptions);
                visitor.visitCode();
                visitor.visitFieldInsn(
                        Opcodes.GETSTATIC,
                        "java/lang/Boolean",
                        "TRUE",
                        "Ljava/lang/Boolean;");
                visitor.visitInsn(Opcodes.ARETURN);
                visitor.visitMaxs(1, 1);
                visitor.visitEnd();
                return null;
            }

            return super.visitMethod(access, name, descriptor, signature, exceptions);
        }
    }
}
