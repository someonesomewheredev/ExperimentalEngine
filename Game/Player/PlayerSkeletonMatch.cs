using System;
using WorldsEngine;
using WorldsEngine.Math;
using WorldsEngine.Util;
using Game.Util;

namespace Game.Player;

[Component]
public class PlayerSkeletonMatch : Component, IStartListener, IUpdateableComponent
{
    private Transform _initialT;
    private Transform _lhToWorld = new(Vector3.Zero, Quaternion.Identity);
    private Transform _rhToWorld = new(Vector3.Zero, Quaternion.Identity);
    private TwoBoneIK _leftHandIK;
    private TwoBoneIK _rightHandIK;
    private TwoBoneIK _legIK;

    public void Start()
    {
        var swo = Entity.GetComponent<SkinnedWorldObject>();
        var root = MeshManager.GetBoneIndex(swo.Mesh, "root");
        _initialT = swo.GetBoneTransform(root);

        var mesh = MeshManager.GetMesh(swo.Mesh);
        var lh = mesh.GetBone("hand_L");
        var rh = mesh.GetBone("hand_R");
        Log.Msg($"lh id: {lh.ID}");
        Log.Msg($"lh name: {lh.Name}");
        Log.Msg($"rh id: {rh.ID}");
        Log.Msg($"rh name: {rh.Name}");

        int parentIdx = (int)lh.Parent;
        Log.Msg($"{lh.RestPose.Rotation}  {swo.GetBoneTransform((uint)lh.ID).Rotation}");

        while (parentIdx != -1)
        {
            Bone b = mesh.GetBone(parentIdx);
            _lhToWorld = _lhToWorld.TransformBy(b.RestPose);
            parentIdx = mesh.GetBone(parentIdx).Parent;
        }

        parentIdx = rh.Parent;

        while (parentIdx != -1)
        {
            Bone b = mesh.GetBone(parentIdx);
            _rhToWorld = _rhToWorld.TransformBy(b.RestPose);
            parentIdx = mesh.GetBone(parentIdx).Parent;
        }

        var lowerArm = mesh.GetBone("lowerarm_L");
        _leftHandIK = new TwoBoneIK(lowerArm.RestPose.Position.Length, mesh.GetBone("hand_L").RestPose.Position.Length, lowerArm.RestPose.Position);

        var lowerArmR = mesh.GetBone("lowerarm_R");
        _rightHandIK = new TwoBoneIK(lowerArm.RestPose.Position.Length, mesh.GetBone("hand_R").RestPose.Position.Length, lowerArm.RestPose.Position);

        var lowerLegR = mesh.GetBone("calf_R");
        _legIK = new TwoBoneIK(lowerLegR.RestPose.Position.Length, mesh.GetBone("foot_R").RestPose.Position.Length, lowerLegR.RestPose.Position);
    }

    public void Update()
    {
        var swo = Entity.GetComponent<SkinnedWorldObject>();
        var root = MeshManager.GetBoneIndex(swo.Mesh, "root");
        var rootTransform = new Transform(Vector3.Down * 0.9f + Vector3.Backward * 0.23f, _initialT.Rotation);
        swo.SetBoneTransform(MeshManager.GetBoneIndex(swo.Mesh, "root"), rootTransform);

        LeftArmUpdate();
        RightArmUpdate();

        UpdateFeetTargets();
        LeftLegUpdate();
        RightLegUpdate();
    }

    private void LeftArmUpdate()
    {
        var swo = Entity.GetComponent<SkinnedWorldObject>();
        var wsTarget = LocalPlayerSystem.LeftHand.Transform;
        var targetTransform = wsTarget.TransformByInverse(Entity.Transform);
        // Fix rotation
        targetTransform.Rotation *= new Quaternion(new Vector3(MathF.PI * 0.25f, 0f, -MathF.PI * 0.25f));
        targetTransform = targetTransform.TransformBy(Entity.Transform);

        var mesh = MeshManager.GetMesh(swo.Mesh);
        var lowerArm = mesh.GetBone("lowerarm_L");
        var upperArm = mesh.GetBone("upperarm_L");
        var hand = mesh.GetBone("hand_L");
        swo.SetBoneWorldSpaceTransform(hand.ID, targetTransform, Entity.Transform);

        var upperArmWS = swo.GetBoneComponentSpaceTransform(upperArm.ID).TransformBy(Entity.Transform);

        // Calculate pole
        Vector3 poleCandidate1 = wsTarget.TransformDirection(Vector3.Right);
        Vector3 poleCandidate2 = Entity.Transform.TransformDirection(Vector3.Right);
        float blendFac = MathF.Pow(1f - MathFX.Saturate(poleCandidate1.Dot(Vector3.Up)), 2.0f);
        Vector3 perpendicularPole = Vector3.Lerp(poleCandidate1, poleCandidate2, blendFac);
        Vector3 pole = Vector3.Cross(upperArmWS.Position.DirectionTo(wsTarget.Position), perpendicularPole);

        var upperRotation = _leftHandIK.GetUpperRotation(upperArmWS, wsTarget, pole);
        upperArmWS.Rotation = upperRotation * new Quaternion(new Vector3(MathF.PI * 0.25f, 0f, MathF.PI * 0.25f));

        swo.SetBoneWorldSpaceTransform(upperArm.ID, upperArmWS, Entity.Transform);

        var lowerArmWS = swo.GetBoneComponentSpaceTransform(lowerArm.ID).TransformBy(Entity.Transform);
        var lowerRotation = _leftHandIK.GetLowerRotation(lowerArmWS, wsTarget, pole);
        lowerArmWS.Rotation = lowerRotation * new Quaternion(new Vector3(MathF.PI * 0.25f, 0f, MathF.PI * 0.25f));
        swo.SetBoneWorldSpaceTransform(lowerArm.ID, lowerArmWS, Entity.Transform);
    }

    private void RightArmUpdate()
    {
        var swo = Entity.GetComponent<SkinnedWorldObject>();
        var wsTarget = LocalPlayerSystem.RightHand.Transform;
        var targetTransform = wsTarget.TransformByInverse(Entity.Transform);
        // Fix rotation
        targetTransform.Rotation *= new Quaternion(new Vector3(MathF.PI * 0.25f, 0f, MathF.PI * 0.25f));
        targetTransform = targetTransform.TransformBy(Entity.Transform);

        var mesh = MeshManager.GetMesh(swo.Mesh);
        var lowerArm = mesh.GetBone("lowerarm_R");
        var upperArm = mesh.GetBone("upperarm_R");
        var hand = mesh.GetBone("hand_R");

        swo.SetBoneWorldSpaceTransform(hand.ID, targetTransform, Entity.Transform);

        var upperArmWS = swo.GetBoneComponentSpaceTransform(upperArm.ID).TransformBy(Entity.Transform);

        // Calculate pole
        Vector3 poleCandidate1 = wsTarget.TransformDirection(Vector3.Left);
        Vector3 poleCandidate2 = Entity.Transform.TransformDirection(Vector3.Left);
        float blendFac = MathF.Pow(1f - MathFX.Saturate(poleCandidate1.Dot(Vector3.Up)), 2.0f);
        Vector3 perpendicularPole = Vector3.Lerp(poleCandidate1, poleCandidate2, blendFac);
        Vector3 pole = Vector3.Cross(upperArmWS.Position.DirectionTo(wsTarget.Position), perpendicularPole);

        var upperRotation = _leftHandIK.GetUpperRotation(upperArmWS, wsTarget, pole);
        upperArmWS.Rotation = upperRotation * new Quaternion(new Vector3(MathF.PI * 0.25f, 0f, MathF.PI * 0.25f));

        swo.SetBoneWorldSpaceTransform(upperArm.ID, upperArmWS, Entity.Transform);

        var lowerArmWS = swo.GetBoneComponentSpaceTransform(lowerArm.ID).TransformBy(Entity.Transform);
        var lowerRotation = _leftHandIK.GetLowerRotation(lowerArmWS, wsTarget, pole);
        lowerArmWS.Rotation = lowerRotation * new Quaternion(new Vector3(MathF.PI * 0.25f, 0f, MathF.PI * 0.25f));
        swo.SetBoneWorldSpaceTransform(lowerArm.ID, lowerArmWS, Entity.Transform);
    }

    private Vector3 _leftFootTarget = new();
    private Vector3 _rightFootTarget = new();

    private Vector3 _nextLeftStep;
    private Vector3 _nextRightStep;
    private float _leftStepProgress = 0.0f;
    private float _rightStepProgress = 0.0f;

    private float PhaseOffset(float v)
    {
        return (v + 0.5f) % 1f;
    }

    private Vector3 _oldLpos;
    private Vector3 _oldRpos;

    private bool _leftStep = false;
    private bool _rightStep = false;
    private float _timeSinceMovement = 0.0f;
    private float _timeSinceMovementStart = 0.0f;
    private bool _standingStraight = false;

    private void UpdateFeetTargets()
    {
        Vector3 groundOffset = new(0.0f, -0.9f, -0.23f);

        const float footX = 0.19f;
        const float stepTime = 0.15f;

        Vector3 localLF = groundOffset + Vector3.Left * footX;
        Vector3 localRF = groundOffset + Vector3.Right * footX;
        Vector3 velocity = Entity.GetComponent<DynamicPhysicsActor>().Velocity;
        float stride = velocity.Length * stepTime * 2f;
        ImGuiNET.ImGui.Text($"stride: {stride}");

        if (velocity.Length < 0.1f)
        {
            _timeSinceMovement += Time.DeltaTime;
            _timeSinceMovementStart = 0.0f;
        }
        else
        {
            _timeSinceMovement = 0.0f;
            _timeSinceMovementStart += Time.DeltaTime;
        }
        ImGuiNET.ImGui.Text($"tsms: {_timeSinceMovementStart}");

        Vector3 movementDir = Entity.Transform.InverseTransformDirection(velocity.Normalized);

        bool funnyStep = false;
        if (_standingStraight && velocity.Length > 0.1f)
        {
            funnyStep = true;
        }

        if (_timeSinceMovement > 0.1f && !_standingStraight)
        {
            _leftStepProgress = 0.0f;
            _oldLpos = _nextLeftStep;
            _nextLeftStep = Entity.Transform.TransformPoint(localLF);
            _leftStep = true;

            _rightStepProgress = 0.0f;
            _oldRpos = _nextRightStep;
            _nextRightStep = Entity.Transform.TransformPoint(localRF);
            _rightStep = true;

            _standingStraight = true;
        }
        else if (_timeSinceMovement < 0.1f)
        {
            _standingStraight = false;
        }


        if (Entity.Transform.TransformPoint(localLF).DistanceTo(_nextLeftStep) > stride || funnyStep)
        {
            _leftStepProgress = 0.0f;
            _oldLpos = _nextLeftStep;
            _nextLeftStep = Entity.Transform.TransformPoint(localLF + movementDir * stride * 0.5f);
            _leftStep = true;

            if (funnyStep) _leftStepProgress = 1f;
        }

        if (_leftStep)
        {
            _leftStepProgress += Time.DeltaTime / stepTime;
            _leftFootTarget = Vector3.Lerp(_oldLpos, _nextLeftStep, _leftStepProgress);
            _leftFootTarget.y += MathF.Sin(_leftStepProgress * MathF.PI) * 0.2f;

            if (_leftStepProgress >= 1f)
            {
                _leftStep = false;
            }
        }

        if (Entity.Transform.TransformPoint(localRF).DistanceTo(_nextRightStep) > stride)
        {
            _rightStepProgress = 0.0f;
            _oldRpos = _nextRightStep;
            _nextRightStep = Entity.Transform.TransformPoint(localRF + movementDir * stride * 0.5f);
            _rightStep = true;
        }

        if (_rightStep)
        {
            _rightStepProgress += Time.DeltaTime / stepTime;
            _rightFootTarget = Vector3.Lerp(_oldRpos, _nextRightStep, _rightStepProgress);
            _rightFootTarget.y += MathF.Sin(_rightStepProgress * MathF.PI) * 0.2f;

            if (_rightStepProgress >= 1f)
            {
                _rightStep = false;
            }
        }
    }

    private void RightLegUpdate()
    {
        var swo = Entity.GetComponent<SkinnedWorldObject>();
        var mesh = MeshManager.GetMesh(swo.Mesh);
        var calf = mesh.GetBone("calf_R");
        var thigh = mesh.GetBone("thigh_R");
        var hand = mesh.GetBone("foot_R");

        var wsTarget = new Transform(_rightFootTarget, Quaternion.Identity);

        var upperArmWS = swo.GetBoneComponentSpaceTransform(thigh.ID).TransformBy(Entity.Transform);

        // Calculate pole
        Vector3 pole = Entity.Transform.TransformDirection(Vector3.Right);

        var upperRotation = _legIK.GetUpperRotation(upperArmWS, wsTarget, pole);
        //upperRotation = upperRotation * Quaternion.AngleAxis(MathF.PI, Vector3.Forward);
        upperArmWS.Rotation = upperRotation * new Quaternion(new Vector3(MathF.PI * 0.25f, 0f, MathF.PI * 0.25f));

        swo.SetBoneWorldSpaceTransform(thigh.ID, upperArmWS, Entity.Transform);

        var lowerArmWS = swo.GetBoneComponentSpaceTransform(calf.ID).TransformBy(Entity.Transform);
        var lowerRotation = _legIK.GetLowerRotation(lowerArmWS, wsTarget, pole);
        //lowerRotation = lowerRotation * Quaternion.AngleAxis(MathF.PI, Vector3.Forward);
        lowerArmWS.Rotation = lowerRotation * new Quaternion(new Vector3(MathF.PI * 0.25f, 0f, MathF.PI * 0.25f));
        swo.SetBoneWorldSpaceTransform(calf.ID, lowerArmWS, Entity.Transform);
    }

    private void LeftLegUpdate()
    {
        var swo = Entity.GetComponent<SkinnedWorldObject>();
        var mesh = MeshManager.GetMesh(swo.Mesh);
        var calf = mesh.GetBone("calf_L");
        var thigh = mesh.GetBone("thigh_L");
        var hand = mesh.GetBone("foot_L");

        var wsTarget = new Transform(_leftFootTarget, Quaternion.Identity);

        var upperArmWS = swo.GetBoneComponentSpaceTransform(thigh.ID).TransformBy(Entity.Transform);

        // Calculate pole
        Vector3 pole = Entity.Transform.TransformDirection(Vector3.Left);

        var upperRotation = _legIK.GetUpperRotation(upperArmWS, wsTarget, pole);
        upperRotation = upperRotation * Quaternion.AngleAxis(MathF.PI, Vector3.Forward);
        upperArmWS.Rotation = upperRotation * new Quaternion(new Vector3(MathF.PI * 0.25f, 0.0f, MathF.PI * 0.25f));

        swo.SetBoneWorldSpaceTransform(thigh.ID, upperArmWS, Entity.Transform);

        var lowerArmWS = swo.GetBoneComponentSpaceTransform(calf.ID).TransformBy(Entity.Transform);
        var lowerRotation = _legIK.GetLowerRotation(lowerArmWS, wsTarget, pole);
        lowerRotation = lowerRotation * Quaternion.AngleAxis(MathF.PI, Vector3.Forward);
        upperArmWS.Rotation = upperRotation * new Quaternion(new Vector3(MathF.PI * 0.25f, 0.0f, MathF.PI * 0.25f));
        lowerArmWS.Rotation = lowerRotation * new Quaternion(new Vector3(MathF.PI * 0.25f, 0f, MathF.PI * 0.25f));
        swo.SetBoneWorldSpaceTransform(calf.ID, lowerArmWS, Entity.Transform);
    }
}