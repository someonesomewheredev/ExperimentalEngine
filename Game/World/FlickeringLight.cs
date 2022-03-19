using WorldsEngine;

namespace Game.World;

[Component]
class FlickeringLight : Component, IThinkingComponent, IStartListener
{
    // Flicker strings go A-Z, where A is minimum brightness and Z is 2x.
    // This string matches the "Fluorescent Flicker" pattern from Quake.
    public string FlickerString = "mmamammmmammamamaaamammma";
    public float FlickerSpeed = 1.0f;
    public int Offset = 0;

    private float _initialIntensity = 0.0f;
    private bool _initiallyEnabled;

    public void Start()
    {
        var worldLight = Registry.GetComponent<WorldLight>(Entity);
        _initialIntensity = worldLight.Intensity;
        _initiallyEnabled = worldLight.Enabled;
    }

    public void Think()
    {
        int index = (int)((Time.CurrentTime * 10 * FlickerSpeed + Offset) % FlickerString.Length);

        int flickerVal = FlickerString[index] - 'a';

        float intensity = flickerVal / 13.0f;

        var worldLight = Registry.GetComponent<WorldLight>(Entity);
        worldLight.Intensity = _initialIntensity * intensity;
        if (intensity == 0.0f)
            worldLight.Enabled = false;
        else
            worldLight.Enabled = _initiallyEnabled;
    }
}