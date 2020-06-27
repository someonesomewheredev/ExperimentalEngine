#version 450
#define PI 3.1415926535

layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec4 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec2 inUV;
layout(location = 4) in float inAO;

const int LT_POINT = 0;
const int LT_SPOT = 1;
const int LT_DIRECTIONAL = 2;

struct Light {
	// (color rgb, type)
	vec4 pack0;
	// (direction xyz, spotlight cutoff)
	vec4 pack1;
	// (position xyz, unused)
	vec4 pack2;
};

layout(std140, binding = 1) uniform LightBuffer {
	vec4 pack0;
	mat4 shadowmapMatrix;
	Light lights[16];
};

layout(std140, binding = 2) uniform MaterialSettingsBuffer {
	// (metallic, roughness, use albedo texture, unused)
	vec4 pack0;
	// (albedo color rgb, unused)
	vec4 pack1;
} Material;

layout (binding = 3) uniform sampler2D albedoSampler;
layout (binding = 4) uniform sampler2D shadowSampler;

layout(push_constant) uniform PushConstants {
	vec4 viewPos;
	mat4 model;
};

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;

    float num = NdotV;
    float denom = NdotV * (1.0f - k) + k;

    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// GGX/Towbridge-Reitz normal distribution function.
// Uses Disney's reparametrization of alpha = roughness^2.
float ndfGGX(float cosLh, float roughness) {
    float alpha = roughness * roughness;
    float alphaSq = alpha * alpha;

    float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
    return alphaSq / (PI * denom * denom);
}

// Single term for separable Schlick-GGX below.
float gaSchlickG1(float cosTheta, float k) {
    return cosTheta / (cosTheta * (1.0 - k) + k);
}

// Schlick-GGX approximation of geometric attenuation function using Smith's method.
float gaSchlickGGX(float cosLi, float cosLo, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; // Epic suggests using this roughness remapping for analytic lights.
    return gaSchlickG1(cosLi, k) * gaSchlickG1(cosLo, k);
}

// Shlick's approximation of the Fresnel factor.
vec3 fresnelSchlick(vec3 F0, float cosTheta) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 calculateLighting(int lightIdx, vec3 viewDir, vec3 f0, float metallic, float roughness) {
	Light light = lights[lightIdx];
	int lightType = int(light.pack0.w);
    vec3 radiance = light.pack0.xyz;
    vec3 L = vec3(0.0f, 0.0f, 0.0f);
	vec3 lightPos = light.pack2.xyz;
	
    if (lightType == LT_POINT) {
        L = lightPos - inWorldPos.xyz;
        // dot(L, L) = length(L) squared
        radiance *= 1.0 / dot(L, L);
    } else if (lightType == LT_SPOT) {
        L = normalize(lightPos - inWorldPos.xyz);
        float theta = dot(L, normalize(light.pack1.xyz));
        float cutoff = light.pack1.w;
        float outerCutoff = cutoff - 0.02f;
        vec3 lToFrag = lightPos - inWorldPos.xyz;
        radiance *= clamp((theta - outerCutoff) / (cutoff - outerCutoff), 0.0f, 1.0f) * (1.0 / dot(lToFrag, lToFrag));
    } else {
        L = light.pack1.xyz;
    }
	
    L = normalize(L);
    // Boost the roughness a little bit for analytical lights otherwise the reflection of the light turns invisible
    roughness = clamp(roughness + 0.05f, 0.0, 1.0);

    vec3 halfway = normalize(viewDir + L);
    vec3 norm = normalize(inNormal);

    float cosLh = max(0.0f, dot(norm, halfway));
    float cosLi = max(0.0f, dot(norm, L));
    float cosLo = max(0.0f, dot(norm, viewDir));

    float NDF = ndfGGX(cosLh, roughness);
    float G = gaSchlickGGX(cosLi, cosLo, roughness);
    vec3 f = fresnelSchlick(f0, max(dot(halfway, viewDir), 0.0f));

    vec3 kd = mix(vec3(1, 1, 1) - f, vec3(0, 0, 0), 0.0);

    vec3 numerator = NDF * G * f;
    float denominator = 4.0f * cosLo * cosLi;
    vec3 specular = numerator / max(denominator, 0.001f);

    vec3 diffuse = kd;
    vec3 lPreShadow = (specular + diffuse) * (radiance * cosLi);

	/*
    if (shadowmapSizeCorners[lightIdx].x > 0.01f && type != LT_DIRECTIONAL) {
        //if (dot(shadowmapSizeCorners[lightIdx], shadowmapSizeCorners[lightIdx]) > 0.01f) {
        float4 lightSpacePos = pixInput.lightSpacePos[(int)positionsShadowIdx[lightIdx].w];
        // Ignore normal maps here
        return lPreShadow * getShadowIntensity(lightIdx, lightSpacePos, pixInput.worldPos, pixInput.norm, L);
    } else if (type == LT_DIRECTIONAL) {
        return lPreShadow * getShadowCascadeIntensity(pixInput.worldPos, pixInput.position.z);
    }*/
    //else {
        return lPreShadow;
    //}
}

void main() {
	//FragColor = vec4(1.0);
	//FragColor = vec4(vec3(inAO), 1.0);
	//return;
	
	//vec3 col = vec3(dot(inNormal, viewDir));
	//FragColor = vec4(col, 1.0);
	//return;
	int lightCount = int(pack0.x);
	
	float metallic = Material.pack0.x;
	float roughness = Material.pack0.y;
	vec3 albedoColor = Material.pack1.rgb;
	
	vec3 viewDir = normalize(viewPos.xyz - inWorldPos.xyz);
	
	vec3 f0 = vec3(0.04f, 0.04f, 0.04f);
    f0 = mix(f0, albedoColor, metallic);
	vec3 lo = vec3(0.05) * inAO;//calculateLighting(0, f0, metallic, roughness);
	//lo += vec3(0.1);
	
	vec4 lightspacePos = shadowmapMatrix * inWorldPos;
    for (int i = 0; i < lightCount; i++) {
		vec3 cLighting = calculateLighting(i, viewDir, f0, metallic, roughness);
		//vec3 cLighting = vec3(1.0);
		if (i == 0) {
			float depth = (lightspacePos.z / lightspacePos.w) - 0.005;
			vec2 texReadPoint = (lightspacePos.xy * 0.5 + 0.5);
			
			if (texReadPoint.x > 0.0 && texReadPoint.x < 1.0 && texReadPoint.y > 0.0 && texReadPoint.y < 1.0 && depth < 1.0 && depth > 0.0) {
				texReadPoint.y = 1.0 - texReadPoint.y;
				float readDepth = texture(shadowSampler, texReadPoint).x;
				if (depth > readDepth)
					cLighting *= 0.0;
			} //else {
				//cLighting = vec3(0.0);
				//lo = vec3(1.0, 0.0, 0.2);
			//}
		}
		lo += cLighting;
	}
	
	// TODO: Ambient stuff - specular cubemaps + irradiance
	
	FragColor = vec4(lo * texture(albedoSampler, inUV).rgb, 1.0);
}