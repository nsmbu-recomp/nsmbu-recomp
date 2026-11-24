interface Hooks {
    onLogicStep?: (this: void, step: number) => void;
    onSceneChange?: (this: void, factory: number) => void;
    onConfigChanged?: (this: void) => void;
    onLoaded?: (this: void) => void;
    onUnload?: (this: void) => void;
}
