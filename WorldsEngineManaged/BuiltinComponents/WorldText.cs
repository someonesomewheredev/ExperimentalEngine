using System;
using System.Runtime.InteropServices;
using System.Text;
using WorldsEngine.ComponentMeta;

namespace WorldsEngine;

public class WorldText : BuiltinComponent
{
    [DllImport(WorldsEngine.NativeModule)]
    private static extern void worldtext_getText(IntPtr regPtr, uint entityId, StringBuilder sb);

    [DllImport(WorldsEngine.NativeModule)]
    private static extern uint worldtext_getTextLength(IntPtr regPtr, uint entityId);

    [DllImport(WorldsEngine.NativeModule)]
    private static extern void worldtext_setText(IntPtr regPtr, uint entityId, [MarshalAs(UnmanagedType.LPStr)] string str);

    internal static ComponentMetadata Metadata
    {
        get
        {
            if (cachedMetadata == null)
                cachedMetadata = MetadataManager.FindNativeMetadata("World Text Component")!;

            return cachedMetadata;
        }
    }

    private static ComponentMetadata? cachedMetadata;

    public string Text
    {
        get
        {
            StringBuilder sb = new((int)worldtext_getTextLength(regPtr, entityId));
            worldtext_getText(regPtr, entityId, sb);
            return sb.ToString();
        }

        set
        {
            worldtext_setText(regPtr, entityId, value);
        }
    }

    internal WorldText(IntPtr regPtr, uint entityId) : base(regPtr, entityId) {}
}