package com.minecraftxbox.lan.mixin;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.InetAddress;
import java.net.MulticastSocket;

import com.minecraftxbox.lan.LanDiscoveryPatch;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

@Mixin(targets = "net.minecraft.class_1134$class_1135", remap = false)
public abstract class LanServerDetectorMixin {
    @Redirect(
        method = "<init>",
        at = @At(
            value = "INVOKE",
            target = "Ljava/net/MulticastSocket;joinGroup(Ljava/net/InetAddress;)V",
            remap = false),
        remap = false,
        require = 0)
    private void minecraftXbox$joinLanGroupOnXboxInterface(MulticastSocket socket, InetAddress group) throws IOException {
        LanDiscoveryPatch.configure(socket);
        socket.joinGroup(group);
    }

    @Redirect(
        method = "run",
        at = @At(
            value = "INVOKE",
            target = "Ljava/net/DatagramPacket;getAddress()Ljava/net/InetAddress;",
            remap = false),
        remap = false,
        require = 0)
    private InetAddress minecraftXbox$useForwardedLanAddress(DatagramPacket packet) {
        InetAddress forwarded = LanDiscoveryPatch.forwardedAddress(packet);
        return forwarded == null ? packet.getAddress() : forwarded;
    }
}
