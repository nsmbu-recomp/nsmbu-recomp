package org.nsmbrecomp.nsmbu;

import java.io.File;

/** Same default as native XDG_CONFIG_HOME=<SDL external files>/config, then /nsmbu. */
public final class AndroidUserDirectories {
    private AndroidUserDirectories() { }

    public static File config(File externalFiles) {
        if (externalFiles == null) throw new IllegalStateException("External app files unavailable");
        return new File(externalFiles, "config/nsmbu");
    }
}
