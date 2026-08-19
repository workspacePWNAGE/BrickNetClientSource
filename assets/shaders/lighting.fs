#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

void main() {
    vec4 texelColor = texture2D(texture0, fragTexCoord);
    
    vec3 lightDir = normalize(vec3(0.35, 1.0, 0.45)); 
    
    vec3 normal = normalize(fragNormal);
    
    if (length(normal) == 0.0) {
        normal = vec3(0.0, 1.0, 0.0);
    }
    
    float dotProduct = dot(normal, lightDir);
    float directionalLight = max(dotProduct, 0.0);
    
    float ambientLight = 0.75;
    float totalLight = ambientLight + (directionalLight * 0.5);
    
    totalLight = clamp(totalLight, 0.0, 1.0);

    finalColor = colDiffuse * fragColor * texelColor * vec4(vec3(totalLight), 1.0);
}