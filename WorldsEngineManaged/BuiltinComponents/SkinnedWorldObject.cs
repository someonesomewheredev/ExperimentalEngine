using System;
using System.Runtime.InteropServices;
using WorldsEngine.ComponentMeta;

namespace WorldsEngine
{
    public class SkinnedWorldObject : BuiltinComponent
    {
        [DllImport(WorldsEngine.NativeModule)]
        private static extern uint skinnedWorldObject_getMesh(IntPtr registryPtr, uint entityId);

        [DllImport(WorldsEngine.NativeModule)]
        private static extern void skinnedWorldObject_setMesh(IntPtr registryPtr, uint entityId, uint meshId);

        [DllImport(WorldsEngine.NativeModule)]
        private static extern uint skinnedWorldObject_getMaterial(IntPtr registryPtr, uint entityId, uint materialIndex);

        [DllImport(WorldsEngine.NativeModule)]
        private static extern void skinnedWorldObject_setMaterial(IntPtr registryPtr, uint entityId, uint materialIndex, uint material);

        [DllImport(WorldsEngine.NativeModule)]
        private static extern char skinnedWorldObject_exists(IntPtr registryPtr, uint entityId);

        [DllImport(WorldsEngine.NativeModule)]
        private static extern void skinnedWorldObject_getBoneTransform(IntPtr registryPtr, uint entityId, uint boneIdx, ref Transform t);

        [DllImport(WorldsEngine.NativeModule)]
        private static extern void skinnedWorldObject_setBoneTransform(IntPtr registryPtr, uint entityId, uint boneIdx, ref Transform t);

        internal static ComponentMetadata Metadata
        {
            get
            {
                if (cachedMetadata == null)
                    cachedMetadata = MetadataManager.FindNativeMetadata("Skinned World Object")!;

                return cachedMetadata;
            }
        }

        private static ComponentMetadata? cachedMetadata;

        const int MAX_MATERIALS = 32;

        public AssetID Mesh
        {
            get => new AssetID(skinnedWorldObject_getMesh(regPtr, entityId));
            set => skinnedWorldObject_setMesh(regPtr, entityId, value.ID);
        }

        internal SkinnedWorldObject(IntPtr regPtr, uint entityId) : base(regPtr, entityId)
        {
        }

        public void SetMaterial(uint idx, AssetID id)
        {
            if (idx >= MAX_MATERIALS)
                throw new ArgumentOutOfRangeException($"Objects can have a maximum of 32 materials. Index {idx} is out of range (0 <= idx < 32)");

            skinnedWorldObject_setMaterial(regPtr, entityId, idx, id.ID);
        }

        public AssetID GetMaterial(uint idx)
        {
            if (idx >= MAX_MATERIALS)
                throw new ArgumentOutOfRangeException($"Objects can have a maximum of 32 materials. Index {idx} is out of range (0 <= idx < 32)");

            return new AssetID(skinnedWorldObject_getMaterial(regPtr, entityId, idx));
        }

        public Transform GetBoneTransform(uint boneIdx)
        {
            Transform t = new();
            skinnedWorldObject_getBoneTransform(regPtr, entityId, boneIdx, ref t);
            return t;
        }

        public void SetBoneTransform(uint boneIdx, Transform t)
        {
            skinnedWorldObject_setBoneTransform(regPtr, entityId, boneIdx, ref t);
        }
    }
}