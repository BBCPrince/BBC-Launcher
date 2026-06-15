package com.minecraftxbox.lan.mixin;

import com.minecraftxbox.lan.LanDiscoveryPatch;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(targets = "net.minecraft.class_1134$class_1136", remap = false)
public abstract class LanServerListMixin {
    @Inject(method = "<init>", at = @At("RETURN"), remap = false, require = 0)
    private void minecraftXbox$startManualLanServerInjector(CallbackInfo ci) {
        LanDiscoveryPatch.startManualServerInjector(this);
    }
}
