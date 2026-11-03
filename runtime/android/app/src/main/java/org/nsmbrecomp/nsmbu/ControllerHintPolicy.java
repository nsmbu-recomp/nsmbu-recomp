package org.nsmbrecomp.nsmbu;

final class ControllerHintPolicy {
    private boolean shown;
    ControllerHintPolicy(boolean remembered) { shown = remembered; }
    boolean claim(boolean controllerConnected) {
        if (!controllerConnected || shown) return false;
        shown = true;
        return true;
    }
}
