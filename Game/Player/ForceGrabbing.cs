using WorldsEngine;
using WorldsEngine.Math;
using Game.Interaction;
using System;

namespace Game.Player;

[Component]
class ForceGrabbing : Component, IStartListener, IThinkingComponent
{
    public bool IsRightHand = false;
    private bool _hoveringEntity = false;
    private bool _bringToHand = false;
    private VRAction _triggerAction;
    private VRAction _grabAction;

    private Vector3 _floatingTargetPos;
    private Entity _lockedEntity = Entity.Null;
    private Entity _lastHoveredEntity = Entity.Null;
    private Vector3 _lowpassedPalmDir;

    [EditableClass]
    public V3PidController PidController = new();
    public float MaxLiftMass = 30.0f;

    public void Start()
    {
        if (!VR.Enabled) return;
        _grabAction = new VRAction(IsRightHand ? "/actions/main/in/GrabR" : "/actions/main/in/GrabL");
        _triggerAction = new VRAction(IsRightHand ? "/actions/main/in/TriggerR" : "/actions/main/in/TriggerL");
    }

    public void Think()
    {
        if (!VR.Enabled) return;
        var hg = Entity.GetComponent<HandGrab>();

        if (hg.GrippedEntity != Entity.Null) return;

        var transform = Entity.Transform;
        // Search outwords from the palm with a cone
        // For now, let's just do a single raycast

        Vector3 palmPos = transform.Position + transform.Forward * 0.1f;
        Vector3 palmDir = transform.TransformDirection(new Vector3(IsRightHand ? 0.75f : -0.75f, 0.0f, 0.25f).Normalized);
        DebugShapes.DrawLine(palmPos, palmPos + palmDir, new Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        _lowpassedPalmDir = Vector3.Lerp(_lowpassedPalmDir, palmDir, Time.DeltaTime * 5.0f);
        DebugShapes.DrawLine(palmPos, palmPos + _lowpassedPalmDir, new Vector4(1.0f, 0.0f, 0.0f, 1.0f));

        if (_bringToHand)
        {
            if (!Registry.Valid(_lockedEntity))
            {
                _hoveringEntity = false;
                _bringToHand = false;
                return;
            }
            var dpa = _lockedEntity.GetComponent<DynamicPhysicsActor>();

            if (_grabAction.Released)
            {
                _hoveringEntity = false;
                _bringToHand = false;
                if (Entity.GetComponent<DynamicPhysicsActor>().Velocity.Dot(palmDir) > 1.0f)
                {
                    dpa.AddForce(_lowpassedPalmDir * 300.0f, ForceMode.Impulse);
                }
            }

            float dist = 1.0f;

            if (_lockedEntity.HasComponent<WorldObject>())
            {
                var obj = _lockedEntity.GetComponent<WorldObject>();
                dist = MeshManager.GetMeshSphereBoundRadius(obj.Mesh) + 0.25f;
            }

            Vector3 targetPos = palmPos + _lowpassedPalmDir * dist;
            dpa.AddForce(PidController.CalculateForce(-(dpa.Pose.Position - targetPos) * MathF.Min(dpa.Mass, MaxLiftMass), Time.DeltaTime, Vector3.Zero));
            return;
        }

        if (!Physics.Raycast(palmPos + palmDir * 0.2f, palmDir, out RaycastHit rayHit, 10.0f, PhysicsLayers.Player))
        {
            _hoveringEntity = false;
            _bringToHand = false;
            return;
        }

        if (rayHit.HitEntity != _lastHoveredEntity)
        {
            _bringToHand = false;
            _hoveringEntity = false;
        }

        if (rayHit.HitEntity.HasComponent<DynamicPhysicsActor>())
        {
            _lastHoveredEntity = rayHit.HitEntity;
            var dpa = rayHit.HitEntity.GetComponent<DynamicPhysicsActor>();
            Vector4 col = new Vector4(1f, 0f, 0f, 1f);

            if (_hoveringEntity)
                col = new Vector4(0f, 1f, 0f, 1f);

            if (_bringToHand)
                col = new Vector4(0f, 0f, 1f, 1f);

            DebugShapes.DrawSphere(rayHit.WorldHitPos, 0.5f, Quaternion.Identity, col);

            if (_grabAction.Pressed)
            {
                _bringToHand = false;
                _hoveringEntity = true;
                _floatingTargetPos = dpa.Pose.Position + Vector3.Up * 0.5f;
                PidController.ResetState();
            }

            if (_grabAction.Released)
            {
                _hoveringEntity = false;
                _bringToHand = false;
            }

            if (_hoveringEntity)
            {
                if (_triggerAction.Pressed)
                {
                    _bringToHand = true;
                    _lockedEntity = rayHit.HitEntity;
                    PidController.ResetState();
                }
            }
        }
    }
}

