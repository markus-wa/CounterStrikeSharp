namespace CounterStrikeSharp.API.Modules.Memory.DynamicFunctions;

public abstract class BaseMemoryFunction : NativeObject
{
    private static Dictionary<string, IntPtr> _createdFunctions = new();

    internal static Dictionary<string, IntPtr> _createdOffsetFunctions = new();

    internal static string FunctionCacheKey(string function, DataType returnType, IEnumerable<DataType> argumentTypes)
    {
        return $"{function}:{returnType}:{string.Join(",", argumentTypes)}";
    }

    private static IntPtr CreateValveFunctionBySignature(string signature, DataType returnType,
        DataType[] argumentTypes)
    {
        var cacheKey = FunctionCacheKey($"{Addresses.ServerPath}:{signature}", returnType, argumentTypes);
        if (!_createdFunctions.TryGetValue(cacheKey, out var function))
        {
            try
            {
                function = NativeAPI.CreateVirtualFunctionBySignature(IntPtr.Zero, Addresses.ServerPath, signature,
                    argumentTypes.Length, (int)returnType, argumentTypes.Cast<object>().ToArray());
                _createdFunctions[cacheKey] = function;
            }
            catch (Exception)
            {
            }
        }

        return function;
    }

    private static IntPtr CreateValveFunctionBySignature(string signature, string binarypath, DataType returnType,
        DataType[] argumentTypes)
    {
        var cacheKey = FunctionCacheKey($"{binarypath}:{signature}", returnType, argumentTypes);
        if (!_createdFunctions.TryGetValue(cacheKey, out var function))
        {
            try
            {
                function = NativeAPI.CreateVirtualFunctionBySignature(IntPtr.Zero, binarypath, signature,
                    argumentTypes.Length, (int)returnType, argumentTypes.Cast<object>().ToArray());
                _createdFunctions[cacheKey] = function;
            }
            catch (Exception)
            {
            }
        }

        return function;
    }

    private static IntPtr CreateValveFunctionByOffset(string symbolName, int offset, DataType returnType,
        DataType[] argumentTypes, Func<nint> nativeCaller)
    {
        string constructKey = FunctionCacheKey($"{symbolName}_{offset}", returnType, argumentTypes);

        if (!_createdOffsetFunctions.TryGetValue(constructKey, out var function))
        {
            try
            {
                function = nativeCaller();
                _createdOffsetFunctions[constructKey] = function;
            }
            catch (Exception)
            {
            }
        }

        return function;
    }

    private static IntPtr CreateValveFunctionByOffset(IntPtr objectPtr, string symbolName, int offset, DataType returnType,
        DataType[] argumentTypes)
    {
        return CreateValveFunctionByOffset(symbolName, offset, returnType, argumentTypes, () =>
        {
            return NativeAPI.CreateVirtualFunction(objectPtr, offset, argumentTypes.Length,
                (int)returnType, argumentTypes.Cast<object>().ToArray());
        });
    }

    private static IntPtr CreateValveFunctionBySymbol(string symbolName, string binaryPath, int offset, DataType returnType,
        DataType[] argumentTypes)
    {
        return CreateValveFunctionByOffset(symbolName, offset, returnType, argumentTypes, () =>
        {
            return NativeAPI.CreateVirtualFunctionBySymbol(binaryPath, symbolName, offset, argumentTypes.Length,
                (int)returnType, argumentTypes.Cast<object>().ToArray());
        });
    }

    private static IntPtr CreateValveFunctionFromVTable(string symbolName, IntPtr vtable, int offset, DataType returnType,
        DataType[] argumentTypes)
    {
        return CreateValveFunctionByOffset(symbolName, offset, returnType, argumentTypes, () =>
        {
            return NativeAPI.CreateVirtualFunctionFromVTable(vtable, offset, argumentTypes.Length,
                (int)returnType, argumentTypes.Cast<object>().ToArray());
        });
    }

    // Set for core functions; bound on first use.
    private readonly CoreFunction? _core;
    private readonly DataType _returnType;
    private readonly DataType[] _parameters = [];

    public BaseMemoryFunction(string signature, DataType returnType, DataType[] parameters) : base(
        CreateValveFunctionBySignature(signature, returnType, parameters))
    {
    }

    public BaseMemoryFunction(string signature, string binarypath, DataType returnType, DataType[] parameters) : base(
        CreateValveFunctionBySignature(signature, binarypath, returnType, parameters))
    {
    }

    /// <summary>
    /// <b>WARNING:</b> this is only supposed to be used with <see cref="VirtualFunctionVoid{TArg1}"/> and <see cref="VirtualFunctionWithReturn{TArg1, TResult}"/> variants.
    /// </summary>
    internal BaseMemoryFunction(IntPtr objectPtr, string symbolName, int offset, DataType returnType, DataType[] parameters) : base(
        CreateValveFunctionByOffset(objectPtr, symbolName, offset, returnType, parameters))
    {
    }

    /// <summary>
    /// <b>WARNING:</b> this is only supposed to be used with <see cref="VirtualFunctionVoid{TArg1}"/> and <see cref="VirtualFunctionWithReturn{TArg1, TResult}"/> variants.
    /// </summary>
    internal BaseMemoryFunction(string symbolName, string binaryPath, int offset, DataType returnType, DataType[] parameters) : base(
        CreateValveFunctionBySymbol(symbolName, binaryPath, offset, returnType, parameters))
    {
    }

    /// <summary>
    /// <b>WARNING:</b> this is only supposed to be used with <see cref="VirtualFunctionVoid{TArg1}"/> and <see cref="VirtualFunctionWithReturn{TArg1, TResult}"/> variants.
    /// </summary>
    internal BaseMemoryFunction(string symbolName, int offset, DataType returnType, DataType[] parameters) : base(
        CreateValveFunctionBySymbol(symbolName, Addresses.ServerPath, offset, returnType, parameters))
    {
    }

    /// <summary>
    /// <b>WARNING:</b> this is only supposed to be used with <see cref="VirtualFunctionVoid{TArg1}"/> and <see cref="VirtualFunctionWithReturn{TArg1, TResult}"/> variants.
    /// </summary>
    internal BaseMemoryFunction(string symbolName, IntPtr vtable, int offset, DataType returnType, DataType[] parameters) : base(
        CreateValveFunctionFromVTable(symbolName, vtable, offset, returnType, parameters))
    {
    }

    /// <summary>
    /// A core memory function, bound lazily from <paramref name="core"/> on first call, hook or <see cref="NativeObject.Handle"/>
    /// read (see <see cref="CoreFunction"/>).
    /// </summary>
    internal BaseMemoryFunction(CoreFunction core, DataType returnType, DataType[] parameters) : base(IntPtr.Zero)
    {
        _core = core;
        _returnType = returnType;
        _parameters = parameters;
    }

    // Binds a core function, throwing EngineFeatureUnavailableException if its Engine Feature is disabled.
    protected override void EnsureNativeHandle()
    {
        if (_core != null) SetHandle(_core.Bind(_returnType, _parameters));
    }

    public void Hook(Func<DynamicHook, HookResult> handler, HookMode mode)
    {
        // On a disabled Engine Feature the hook registers but never fires (logged once), instead of throwing.
        if (_core != null && !_core.CanHook()) return;

        NativeAPI.HookFunction(Handle, handler, mode == HookMode.Post);
    }

    public void Unhook(Func<DynamicHook, HookResult> handler, HookMode mode)
    {
        if (_core != null && !_core.CanUnhook()) return;

        NativeAPI.UnhookFunction(Handle, handler, mode == HookMode.Post);
    }

    protected T InvokeInternal<T>(bool bypass, params object[] args)
    {
        return NativeAPI.ExecuteVirtualFunction<T>(Handle, bypass, args);
    }

    protected void InvokeInternalVoid(bool bypass, params object[] args)
    {
        NativeAPI.ExecuteVirtualFunction<object>(Handle, bypass, args);
    }
}