﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using WorldsEngine;
using WorldsEngine.Input;
using WorldsEngine.Math;
using WorldsEngine.Audio;
using ImGuiNET;

namespace Game
{
    [Component]
    [EditorFriendlyName("C# Player Rig")]
    public class PlayerRig : IThinkingComponent, IStartListener
    {
        const float LocosphereRadius = 0.15f;

        [EditableClass]
        public V3PidController pidController = new V3PidController();

        private bool _grounded = false;
        private bool _groundedLast = false;
        private VRAction _movementAction;
        private Vector3 _lastHMDPos = Vector3.Zero;
        private float _footstepTimer = 0.0f;
        private float _timeSinceDodge = 1000.0f;
        private float _timeSinceJump = 0.0f;
        private Vector3 _lastDodgeDirection = Vector3.Zero;

        private Vector2 GetInputVelocity()
        {
            if (FreecamSystem.Enabled) return Vector2.Zero;
            Vector2 inputVel = new Vector2();

            if (Keyboard.KeyHeld(KeyCode.W))
            {
                inputVel.y += 1.0f;
            }

            if (Keyboard.KeyHeld(KeyCode.S))
            {
                inputVel.y -= 1.0f;
            }

            if (Keyboard.KeyHeld(KeyCode.A))
            {
                inputVel.x += 1.0f;
            }

            if (Keyboard.KeyHeld(KeyCode.D))
            {
                inputVel.x -= 1.0f;
            }

            if (VR.Enabled)
            {
                if (_movementAction == null)
                    _movementAction = new VRAction("/actions/main/in/Movement");

                inputVel = _movementAction.Vector2Value;
                inputVel.x = -inputVel.x;
            }

            return inputVel;
        }

        private void UpdateDodge(Entity entity)
        {
            var dpa = Registry.GetComponent<DynamicPhysicsActor>(entity);

            _timeSinceDodge += Time.DeltaTime;

            if (_timeSinceDodge > 0.5f)
            {
                if (Keyboard.KeyPressed(KeyCode.NumberRow1))
                {
                    Vector3 dodgeDir = Camera.Main.Rotation * Vector3.Left;
                    _lastDodgeDirection = dodgeDir;
                    dpa.AddForce(Camera.Main.Rotation * Vector3.Left * 15f, ForceMode.VelocityChange);
                    _timeSinceDodge = 0.0f;
                }

                if (Keyboard.KeyPressed(KeyCode.NumberRow2))
                {
                    Vector3 dodgeDir = Camera.Main.Rotation * Vector3.Right;
                    _lastDodgeDirection = dodgeDir;
                    dpa.AddForce(Camera.Main.Rotation * Vector3.Right * 15f, ForceMode.VelocityChange);
                    _timeSinceDodge = 0.0f;
                }
            }
        }

        private void UpdateSound(Entity entity, Vector3 inputDirCS)
        {
            if (_grounded && !_groundedLast)
            {
                Audio.PlayOneShotEvent("event:/Player/Land", Vector3.Zero);
            }

            if (inputDirCS.LengthSquared > 0.0f && _grounded)
            {
                _footstepTimer += Time.DeltaTime * 2f;
            }

            if (_footstepTimer >= 1.0f)
            {
                Audio.PlayOneShotEvent("event:/Player/Walking", Vector3.Zero);
                _footstepTimer = 0f;
            }
        }

        public void Think(Entity entity)
        {
            var dpa = Registry.GetComponent<DynamicPhysicsActor>(entity);

            Vector2 inputVel = GetInputVelocity();
            float max = MathF.Max(MathF.Abs(inputVel.x), MathF.Abs(inputVel.y));
            inputVel.Normalize();

            Vector3 inputDir = new Vector3(inputVel.x, 0.0f, inputVel.y);

            Vector3 inputDirCS = Camera.Main.Rotation * inputDir;

            if (VR.Enabled)
            {
                inputDirCS = VR.HMDTransform.Rotation * Camera.Main.Rotation * inputDir;
            }

            inputDirCS.y = 0.0f;
            inputDirCS.Normalize();

            //_grounded = Physics.Raycast(
            //    dpa.Pose.Position,
            //    Vector3.Down,
            //    out RaycastHit hit,
            //    1.1f,
            //    PhysicsLayers.Player
            //);

            _grounded = Physics.SweepSphere(
                dpa.Pose.Position,
                0.1f,
                Vector3.Down,
                1.15f,
                out RaycastHit hit,
                PhysicsLayers.Player
            );

            Vector3 targetPosition = hit.WorldHitPos + (Vector3.Up * 1.15f);

            if (_grounded && PlayerRigSystem.Jump)
            {
                dpa.Velocity = (dpa.Velocity * new Vector3(1.0f, 0.0f, 1.0f)) + Vector3.Up * 4.0f;
                _timeSinceJump = 0.0f;
                Audio.PlayOneShotEvent("event:/Player/Jump", Vector3.Zero);
            }
            PlayerRigSystem.Jump = false;

            if (_grounded && _timeSinceJump > 0.1f)
                dpa.AddForce(pidController.CalculateForce(targetPosition - dpa.Pose.Position, Time.DeltaTime));

            Vector3 targetVelocity = inputDirCS * 7.5f * max;
            Vector3 appliedVelocity = (targetVelocity - dpa.Velocity) * (_grounded ? 10f : 2.5f);
            appliedVelocity.y = 0.0f;

            if (_timeSinceDodge < 0.1f)
            {
                appliedVelocity -= _lastDodgeDirection * Vector3.Dot(appliedVelocity, _lastDodgeDirection) * (1.0f - (_timeSinceDodge * 10f));
            }

            if (_grounded || targetVelocity.LengthSquared > 0.01f)
                dpa.AddForce(appliedVelocity, ForceMode.Acceleration);

            if (VR.Enabled)
            {
                Vector3 movement = VR.HMDTransform.Position - _lastHMDPos;
                movement.y = 0f;
                var pose = dpa.Pose;
                pose.Position += movement;
                dpa.Pose = pose;
                _lastHMDPos = VR.HMDTransform.Position;
            }

            UpdateSound(entity, inputDirCS);
            UpdateDodge(entity);

            _groundedLast = _grounded;
            _timeSinceJump += Time.DeltaTime;
        }

        public void Start(Entity entity)
        {
            if (VR.Enabled)
                _lastHMDPos = VR.HMDTransform.Position;
        }
    }

    [SystemUpdateOrder(-2)]
    public class PlayerRigSystem : ISystem
    {
        public static Entity PlayerBody { get; private set; }
        public static Entity PlayerFender { get; private set; }

        public static bool Jump = false;

        private VRAction _jumpAction;
        private Entity _hpTextEntity;
        private Entity _leftHandEntity;

        public void OnSceneStart()
        {
            if (VR.Enabled)
                _jumpAction = new VRAction("/actions/main/in/Jump");

            PlayerBody = Registry.Find("Player Body");

            if (PlayerBody.IsNull) return;

            PlayerFender = Registry.Find("Fender");

            var healthComp = Registry.GetComponent<Combat.HealthComponent>(PlayerBody);
            healthComp.OnDeath += (Entity e) => {
                SceneLoader.LoadScene(SceneLoader.CurrentSceneID);
            };

            _hpTextEntity = Registry.Create();
            Registry.AddComponent<WorldText>(_hpTextEntity);
            _leftHandEntity = Registry.Find("LeftHand");
        }

        public void OnUpdate()
        {
            if (PlayerBody.IsNull) return;

            if (Keyboard.KeyPressed(KeyCode.Space))
                Jump = true;

            if (VR.Enabled && _jumpAction.Held)
            {
                Jump = true;
            }

            var hpText = Registry.GetComponent<WorldText>(_hpTextEntity);
            hpText.Size = 0.001f;
            hpText.Text = $"HP: {Registry.GetComponent<Combat.HealthComponent>(PlayerBody).Health}";

            var lhTransform = Registry.GetTransform(_leftHandEntity);
            var hpTransform = Registry.GetTransform(_hpTextEntity);
            hpTransform.Rotation = lhTransform.Rotation * 
                Quaternion.AngleAxis(MathF.PI / 2f, Vector3.Left) * Quaternion.AngleAxis(MathF.PI, Vector3.Up);
            Vector3 offset = Vector3.Forward * 0.05f + Vector3.Down * 0.02f;
            hpTransform.Position = lhTransform.Position + hpTransform.Rotation * offset;
            Registry.SetTransform(_hpTextEntity, hpTransform);
        }
    }
}
