"""Turn NSMBU_* settings into game flags and keep them out of a child environment."""


def flag_name(env_name):
    name = env_name[6:] if str(env_name).startswith("NSMBU_") else str(env_name)
    return "--" + name.lower().replace("_", "-")


def args_from_env(mapping):
    args = []
    for key, value in mapping.items():
        if value is None or not str(key).startswith("NSMBU_"):
            continue
        flag = flag_name(key)
        text = str(value)
        if text == "1":
            args.append(flag)
        else:
            args.append("%s=%s" % (flag, text))
    return args


def has_flag(args, flag):
    for arg in args:
        if arg == flag or str(arg).startswith(flag + "="):
            return True
    return False


def strip_runtime(env):
    out = dict(env)
    for key in list(out):
        if key.startswith("NSMBU_") or key == "WIIU_COMMON_KEY":
            del out[key]
    return out
