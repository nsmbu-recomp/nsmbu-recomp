package org.nsmbrecomp.nsmbu;

/** Shared wording for the setup screen and its foreground notification. */
public final class SetupPresentation {
    private SetupPresentation() {}
    public static final String UPDATING = "Updating the game for the new version…";
    public static String stageLabel(String stage) {
        switch (stage) {
            case "extract": return "Extracting the game";
            case "translate": return "Translating the game code";
            case "compile": return "Compiling the game code";
            case "link": case "activate": return "Finishing";
            default: return stage;
        }
    }
    public static boolean showUpdate(boolean updating, String state) {
        return updating && !"complete".equals(state) && !"failed".equals(state);
    }
}
