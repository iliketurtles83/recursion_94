/**
 * RaymarchPass.ts
 *
 * Full-screen raymarching pass synthesizing an infinite, scale-invariant
 * recursive KIFS fractal world and dynamic isosurface soft bodies via polynomial smin.
 */

export interface Vector3Like {
    x: number;
    y: number;
    z: number;
}

export interface CameraUniformSource {
    position: Vector3Like | [number, number, number];
    target?: Vector3Like | [number, number, number];
    up?: Vector3Like | [number, number, number];
    fovY?: number;
    aspect?: number;
    near?: number;
    far?: number;
    viewMatrix?: Float32Array | number[];
    projectionMatrix?: Float32Array | number[];
    invViewProj?: Float32Array | number[];
}

export interface RaymarchPassOptions {
    iterations?: number;
    scale?: number;
    offset?: [number, number, number];
    useGBuffer?: boolean;
}

export interface EntityUniformDataInput {
    entityCount: number;
    positions: Float32Array;
    radii: Float32Array;
    colors: Float32Array;
    blendRadii: Float32Array;
    packedBuffer?: Float32Array;
}

// 4x4 Matrix operations for computing u_invViewProj without external libraries
function mat4Multiply(out: Float32Array, a: ArrayLike<number>, b: ArrayLike<number>): Float32Array {
    for (let row = 0; row < 4; ++row) {
        for (let col = 0; col < 4; ++col) {
            let sum = 0.0;
            for (let k = 0; k < 4; ++k) {
                sum += a[row + k * 4] * b[k + col * 4];
            }
            out[row + col * 4] = sum;
        }
    }
    return out;
}

function mat4Invert(out: Float32Array, m: ArrayLike<number>): boolean {
    const inv = new Float32Array(16);

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];

    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
              m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];

    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];

    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
               m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
              m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];

    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];

    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
              m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];

    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];

    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
              m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];

    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];

    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
               m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
              m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
               m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    let det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (Math.abs(det) < 1e-8) {
        return false;
    }

    det = 1.0 / det;
    for (let i = 0; i < 16; i++) {
        out[i] = inv[i] * det;
    }
    return true;
}

function mat4Perspective(out: Float32Array, fovyRad: number, aspect: number, near: number, far: number): Float32Array {
    const f = 1.0 / Math.tan(fovyRad / 2.0);
    out.fill(0);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = (far + near) / (near - far);
    out[11] = -1.0;
    out[14] = (2.0 * far * near) / (near - far);
    return out;
}

function mat4LookAt(
    out: Float32Array,
    eye: [number, number, number],
    center: [number, number, number],
    up: [number, number, number]
): Float32Array {
    let z0 = eye[0] - center[0];
    let z1 = eye[1] - center[1];
    let z2 = eye[2] - center[2];
    let len = Math.hypot(z0, z1, z2);
    if (len < 1e-6) len = 1;
    z0 /= len; z1 /= len; z2 /= len;

    let x0 = up[1] * z2 - up[2] * z1;
    let x1 = up[2] * z0 - up[0] * z2;
    let x2 = up[0] * z1 - up[1] * z0;
    len = Math.hypot(x0, x1, x2);
    if (len < 1e-6) len = 1;
    x0 /= len; x1 /= len; x2 /= len;

    const y0 = z1 * x2 - z2 * x1;
    const y1 = z2 * x0 - z0 * x2;
    const y2 = z0 * x1 - z1 * x0;

    out[0] = x0; out[1] = y0; out[2] = z0; out[3] = 0;
    out[4] = x1; out[5] = y1; out[6] = z1; out[7] = 0;
    out[8] = x2; out[9] = y2; out[10] = z2; out[11] = 0;
    out[12] = -(x0 * eye[0] + x1 * eye[1] + x2 * eye[2]);
    out[13] = -(y0 * eye[0] + y1 * eye[1] + y2 * eye[2]);
    out[14] = -(z0 * eye[0] + z1 * eye[1] + z2 * eye[2]);
    out[15] = 1;
    return out;
}

export class RaymarchPass {
    private gl: WebGL2RenderingContext;
    private program: WebGLProgram | null = null;
    private quadVAO: WebGLVertexArrayObject | null = null;
    private quadVBO: WebGLBuffer | null = null;

    // Intermediate G-Buffer / Render Targets
    private gBufferFBO: WebGLFramebuffer | null = null;
    private colorTexture: WebGLTexture | null = null;
    private depthTexture: WebGLTexture | null = null;
    private width: number = 1920;
    private height: number = 1080;

    // Uniform Locations
    private locResolution: WebGLUniformLocation | null = null;
    private locTime: WebGLUniformLocation | null = null;
    private locCamPos: WebGLUniformLocation | null = null;
    private locInvViewProj: WebGLUniformLocation | null = null;
    private locIterations: WebGLUniformLocation | null = null;
    private locScale: WebGLUniformLocation | null = null;
    private locOffset: WebGLUniformLocation | null = null;

    // Entity Uniform Locations
    private locEntityCount: WebGLUniformLocation | null = null;
    private locEntities: {
        position: WebGLUniformLocation | null;
        radius: WebGLUniformLocation | null;
        color: WebGLUniformLocation | null;
        blendRadius: WebGLUniformLocation | null;
    }[] = [];

    // Settings
    public iterations: number = 6;
    public scale: number = 1.85;
    public offset: [number, number, number] = [1.0, 0.5, 1.0];
    public useGBuffer: boolean = true;

    // Matrix buffers
    private viewProj = new Float32Array(16);
    private invViewProj = new Float32Array(16);
    private tempView = new Float32Array(16);
    private tempProj = new Float32Array(16);

    constructor(gl: WebGL2RenderingContext, options?: RaymarchPassOptions) {
        this.gl = gl;
        if (options?.iterations !== undefined) this.iterations = options.iterations;
        if (options?.scale !== undefined) this.scale = options.scale;
        if (options?.offset !== undefined) this.offset = options.offset;
        if (options?.useGBuffer !== undefined) this.useGBuffer = options.useGBuffer;

        this.initGeometry();
        this.initShaders();
        this.initGBuffer(this.width, this.height);
    }

    /**
     * Instantiates a fullscreen quad spanning normalized device coordinates [-1, -1] to [1, 1]
     */
    private initGeometry(): void {
        const gl = this.gl;
        const quadVertices = new Float32Array([
            -1.0, -1.0,
             1.0, -1.0,
            -1.0,  1.0,
            -1.0,  1.0,
             1.0, -1.0,
             1.0,  1.0,
        ]);

        this.quadVAO = gl.createVertexArray();
        gl.bindVertexArray(this.quadVAO);

        this.quadVBO = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.quadVBO);
        gl.bufferData(gl.ARRAY_BUFFER, quadVertices, gl.STATIC_DRAW);

        gl.enableVertexAttribArray(0);
        gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);

        gl.bindVertexArray(null);
        gl.bindBuffer(gl.ARRAY_BUFFER, null);
    }

    private initShaders(): void {
        const gl = this.gl;

        const vsSource = `#version 300 es
        layout(location = 0) in vec2 a_position;
        out vec2 v_uv;
        void main() {
            v_uv = a_position * 0.5 + 0.5;
            gl_Position = vec4(a_position, 0.0, 1.0);
        }`;

        const fsSource = `#version 300 es
        precision highp float;
        precision highp int;

        uniform vec2 u_resolution;
        uniform float u_time;
        uniform vec3 u_camPos;
        uniform mat4 u_invViewProj;
        uniform int u_iterations;
        uniform float u_scale;
        uniform vec3 u_offset;

        // Dynamic Entity Storage (up to 16 soft bodies)
        struct EntityData {
            vec3 position;
            float radius;
            vec3 color;
            float blendRadius; // 'k' parameter
        };

        uniform int u_entityCount;
        uniform EntityData u_entities[16];

        in vec2 v_uv;
        layout(location = 0) out vec4 fragColor;

        // Polynomial Smooth Minimum
        struct SminResult {
            float dist;
            float factor;
        };

        SminResult smin(float a, float b, float k) {
            float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
            float d = mix(b, a, h) - k * h * (1.0 - h);
            return SminResult(d, h);
        }

        float sdBox(vec3 p, vec3 b) {
            vec3 q = abs(p) - b;
            return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
        }

        float sdSphere(vec3 p, float r) {
            return length(p) - r;
        }

        float mapScene(vec3 p) {
            vec3 w = p;
            float scale = 1.0;
            int iters = (u_iterations > 0) ? u_iterations : 6;
            float s = (u_scale > 0.0) ? u_scale : 1.85;
            vec3 off = (length(u_offset) > 0.0) ? u_offset : vec3(1.0, 0.5, 1.0);

            for (int i = 0; i < iters; ++i) {
                // 1. Fold across planes of symmetry
                w = abs(w) - vec3(1.2, 0.8, 1.2);
                if (w.x < w.y) w.xy = w.yx;
                if (w.x < w.z) w.xz = w.zx;
                if (w.y < w.z) w.yz = w.zy;

                // 2. Scale and translate
                w = w * s - off * (s - 1.0);
                scale *= s;
            }
            // Normalize terminal distance to prevent stepping past boundaries
            return (length(w) - 0.4) / scale;
        }

        float kifsFold(vec3 p) {
            return mapScene(p);
        }

        // Composite Entity SDF Mapping
        vec4 mapEntities(vec3 p) {
            if (u_entityCount <= 0) {
                return vec4(1e5, 0.0, 0.0, 0.0);
            }

            float accumDist = length(p - u_entities[0].position) - u_entities[0].radius;
            vec3 accumColor = u_entities[0].color;

            for (int i = 1; i < 16; ++i) {
                if (i >= u_entityCount) break;
                EntityData ent = u_entities[i];
                float d = length(p - ent.position) - ent.radius;
                float k = max(ent.blendRadius, 0.0001);
                SminResult res = smin(accumDist, d, k);
                accumColor = mix(ent.color, accumColor, res.factor);
                accumDist = res.dist;
            }

            return vec4(accumDist, accumColor);
        }

        // Combined world + entity distance evaluator
        float mapSceneWithEntities(vec3 p) {
            float worldDist = mapScene(p);
            if (u_entityCount <= 0) return worldDist;
            vec4 ent = mapEntities(p);
            return min(worldDist, ent.x);
        }

        // Combined distance and surface color evaluator
        vec4 mapSceneWithEntitiesColor(vec3 p) {
            float worldDist = mapScene(p);
            vec3 worldColor = mix(
                vec3(0.12, 0.74, 0.92),
                vec3(0.92, 0.28, 0.62),
                0.5 + 0.5 * sin(p.z * 0.18 + u_time * 0.35)
            );

            if (u_entityCount <= 0) {
                return vec4(worldDist, worldColor);
            }

            vec4 ent = mapEntities(p);
            if (ent.x < worldDist) {
                return ent;
            }
            return vec4(worldDist, worldColor);
        }

        vec3 calcNormal(vec3 p) {
            const vec2 k = vec2(1.0, -1.0) * 0.0005;
            return normalize(
                k.xyy * mapSceneWithEntities(p + k.xyy) +
                k.yyx * mapSceneWithEntities(p + k.yyx) +
                k.yxy * mapSceneWithEntities(p + k.yxy) +
                k.xxx * mapSceneWithEntities(p + k.xxx)
            );
        }

        float raymarch(vec3 ro, vec3 rd, float maxDist, int maxSteps, out int stepsOut) {
            float t = 0.0;
            float candidate_error = 0.0;
            const float omega = 1.2;
            stepsOut = 0;

            for (int i = 0; i < maxSteps; ++i) {
                stepsOut = i + 1;
                vec3 p = ro + rd * t;
                float d = mapSceneWithEntities(p);

                if (omega > 1.0 && (d + candidate_error) < candidate_error) {
                    t -= candidate_error;
                    d = mapSceneWithEntities(ro + rd * t);
                    t += d;
                    candidate_error = 0.0;
                } else {
                    candidate_error = d * (omega - 1.0);
                    t += d * omega;
                }

                float eps = max(0.0002, 0.001 * t);
                if (d < eps) return t;
                if (t >= maxDist) break;
            }
            return -1.0;
        }

        void main() {
            vec2 ndc = v_uv * 2.0 - 1.0;
            vec4 pNearClip = u_invViewProj * vec4(ndc, -1.0, 1.0);
            vec4 pFarClip  = u_invViewProj * vec4(ndc,  1.0, 1.0);
            vec3 pNear = pNearClip.xyz / pNearClip.w;
            vec3 pFar  = pFarClip.xyz  / pFarClip.w;

            vec3 ro = u_camPos;
            vec3 rd = normalize(pFar - pNear);

            const int maxSteps = 80;
            const float maxDist = 90.0;
            int steps = 0;
            float t = raymarch(ro, rd, maxDist, maxSteps, steps);

            if (t >= 0.0) {
                vec3 hitPos = ro + rd * t;
                vec3 normal = calcNormal(hitPos);

                vec3 keyLight = normalize(vec3(0.577, 0.78, -0.45));
                float diff = max(dot(normal, keyLight), 0.0);

                float ao = clamp(float(steps) / float(maxSteps), 0.0, 1.0);
                float ambient = 1.0 - ao * 0.82;

                vec4 sceneColorData = mapSceneWithEntitiesColor(hitPos);
                vec3 baseColor = sceneColorData.yzw;
                vec3 lit = baseColor * (diff * 0.85 + 0.15) * ambient;

                vec3 viewDir = normalize(ro - hitPos);
                vec3 halfDir = normalize(keyLight + viewDir);
                float spec = pow(max(dot(normal, halfDir), 0.0), 16.0);
                lit += vec3(0.8, 0.9, 1.0) * spec * 0.25 * ambient;

                float fog = 1.0 - exp(-t * 0.022);
                vec3 fogColor = vec3(0.012, 0.022, 0.055);
                fragColor = vec4(mix(lit, fogColor, clamp(fog, 0.0, 1.0)), 1.0);
            } else {
                fragColor = vec4(0.0, 0.0, 0.0, 0.0);
            }
        }`;

        const vs = this.compileShader(gl.VERTEX_SHADER, vsSource);
        const fs = this.compileShader(gl.FRAGMENT_SHADER, fsSource);

        this.program = gl.createProgram()!;
        gl.attachShader(this.program, vs);
        gl.attachShader(this.program, fs);
        gl.linkProgram(this.program);

        if (!gl.getProgramParameter(this.program, gl.LINK_STATUS)) {
            console.error("RaymarchPass program linking failed:", gl.getProgramInfoLog(this.program));
        }

        gl.deleteShader(vs);
        gl.deleteShader(fs);

        // Cache uniform locations
        this.locResolution = gl.getUniformLocation(this.program, "u_resolution");
        this.locTime = gl.getUniformLocation(this.program, "u_time");
        this.locCamPos = gl.getUniformLocation(this.program, "u_camPos");
        this.locInvViewProj = gl.getUniformLocation(this.program, "u_invViewProj");
        this.locIterations = gl.getUniformLocation(this.program, "u_iterations");
        this.locScale = gl.getUniformLocation(this.program, "u_scale");
        this.locOffset = gl.getUniformLocation(this.program, "u_offset");

        // Cache entity uniform locations
        this.locEntityCount = gl.getUniformLocation(this.program, "u_entityCount");
        this.locEntities = [];
        for (let i = 0; i < 16; ++i) {
            this.locEntities.push({
                position: gl.getUniformLocation(this.program, `u_entities[${i}].position`),
                radius: gl.getUniformLocation(this.program, `u_entities[${i}].radius`),
                color: gl.getUniformLocation(this.program, `u_entities[${i}].color`),
                blendRadius: gl.getUniformLocation(this.program, `u_entities[${i}].blendRadius`)
            });
        }
    }

    private compileShader(type: number, source: string): WebGLShader {
        const gl = this.gl;
        const shader = gl.createShader(type)!;
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
            console.error("Shader compilation error:", gl.getShaderInfoLog(shader));
        }
        return shader;
    }

    /**
     * Sets up intermediate G-buffer (color + linear depth textures)
     */
    public initGBuffer(width: number, height: number): void {
        const gl = this.gl;
        this.width = width;
        this.height = height;

        if (this.gBufferFBO) {
            gl.deleteFramebuffer(this.gBufferFBO);
            if (this.colorTexture) gl.deleteTexture(this.colorTexture);
            if (this.depthTexture) gl.deleteTexture(this.depthTexture);
        }

        this.gBufferFBO = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, this.gBufferFBO);

        // Color attachment (RGBA8)
        this.colorTexture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this.colorTexture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, this.colorTexture, 0);

        // Depth attachment (DEPTH_COMPONENT24)
        this.depthTexture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this.depthTexture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT24, width, height, 0, gl.DEPTH_COMPONENT, gl.UNSIGNED_INT, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, this.depthTexture, 0);

        const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
        if (status !== gl.FRAMEBUFFER_COMPLETE) {
            console.error("RaymarchPass G-Buffer framebuffer incomplete:", status);
        }

        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.bindTexture(gl.TEXTURE_2D, null);
    }

    /**
     * Updates uniforms every tick from the active gameplay camera and timing subsystem.
     */
    public updateUniforms(camera: CameraUniformSource, timeSeconds: number): void {
        const gl = this.gl;
        if (!this.program) return;
        gl.useProgram(this.program);

        // Viewport resolution
        if (this.locResolution) {
            gl.uniform2f(this.locResolution, this.width, this.height);
        }

        // Time
        if (this.locTime) {
            gl.uniform1f(this.locTime, timeSeconds);
        }

        // Camera position
        const camPos: [number, number, number] = Array.isArray(camera.position)
            ? camera.position
            : [camera.position.x, camera.position.y, camera.position.z];

        if (this.locCamPos) {
            gl.uniform3f(this.locCamPos, camPos[0], camPos[1], camPos[2]);
        }

        // Compute or assign inverse View-Projection matrix
        if (camera.invViewProj) {
            this.invViewProj.set(camera.invViewProj);
        } else if (camera.viewMatrix && camera.projectionMatrix) {
            mat4Multiply(this.viewProj, camera.projectionMatrix, camera.viewMatrix);
            mat4Invert(this.invViewProj, this.viewProj);
        } else {
            const aspect = camera.aspect ?? (this.width / Math.max(this.height, 1));
            const fovY = (camera.fovY ?? 72.0) * (Math.PI / 180.0);
            const near = camera.near ?? 0.1;
            const far = camera.far ?? 1000.0;
            const target: [number, number, number] = camera.target
                ? (Array.isArray(camera.target) ? camera.target : [camera.target.x, camera.target.y, camera.target.z])
                : [camPos[0], camPos[1], camPos[2] - 10.0];
            const up: [number, number, number] = camera.up
                ? (Array.isArray(camera.up) ? camera.up : [camera.up.x, camera.up.y, camera.up.z])
                : [0, 1, 0];

            mat4Perspective(this.tempProj, fovY, aspect, near, far);
            mat4LookAt(this.tempView, camPos, target, up);
            mat4Multiply(this.viewProj, this.tempProj, this.tempView);
            mat4Invert(this.invViewProj, this.viewProj);
        }

        if (this.locInvViewProj) {
            gl.uniformMatrix4fv(this.locInvViewProj, false, this.invViewProj);
        }

        // Fractal parameters
        if (this.locIterations) gl.uniform1i(this.locIterations, this.iterations);
        if (this.locScale) gl.uniform1f(this.locScale, this.scale);
        if (this.locOffset) gl.uniform3f(this.locOffset, this.offset[0], this.offset[1], this.offset[2]);
    }

    /**
     * Updates dynamic entity uniforms on the GPU from the SoftBodySystem or packed uniform data.
     */
    public updateEntityUniforms(
        source: EntityUniformDataInput | { packEntityUniforms: () => EntityUniformDataInput }
    ): void {
        const gl = this.gl;
        if (!this.program) return;
        gl.useProgram(this.program);

        const data: EntityUniformDataInput = ('packEntityUniforms' in source && typeof source.packEntityUniforms === 'function')
            ? source.packEntityUniforms()
            : (source as EntityUniformDataInput);

        const count = Math.min(data.entityCount, 16);

        if (this.locEntityCount) {
            gl.uniform1i(this.locEntityCount, count);
        }

        for (let i = 0; i < count; ++i) {
            const loc = this.locEntities[i];
            if (!loc) continue;

            if (loc.position) {
                gl.uniform3f(loc.position, data.positions[i * 3 + 0], data.positions[i * 3 + 1], data.positions[i * 3 + 2]);
            }
            if (loc.radius) {
                gl.uniform1f(loc.radius, data.radii[i]);
            }
            if (loc.color) {
                gl.uniform3f(loc.color, data.colors[i * 3 + 0], data.colors[i * 3 + 1], data.colors[i * 3 + 2]);
            }
            if (loc.blendRadius) {
                gl.uniform1f(loc.blendRadius, data.blendRadii[i]);
            }
        }
    }

    /**
     * Executes the render pass targeting the main frame buffer or intermediate G-buffer.
     */
    public render(targetFBO: WebGLFramebuffer | null = null): void {
        const gl = this.gl;
        if (!this.program || !this.quadVAO) return;

        const fboToBind = (targetFBO !== null) ? targetFBO : (this.useGBuffer ? this.gBufferFBO : null);
        gl.bindFramebuffer(gl.FRAMEBUFFER, fboToBind);
        gl.viewport(0, 0, this.width, this.height);

        gl.useProgram(this.program);
        gl.bindVertexArray(this.quadVAO);

        gl.drawArrays(gl.TRIANGLES, 0, 6);

        gl.bindVertexArray(null);
        gl.useProgram(null);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    }

    /**
     * Exports the resulting depth buffer or blits it down the pipeline so HUD/rasterized
     * gameplay elements can properly occlude against the fractal geometry.
     */
    public blitDepthTo(destinationFBO: WebGLFramebuffer | null, dstWidth: number, dstHeight: number): void {
        const gl = this.gl;
        if (!this.gBufferFBO) return;

        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.gBufferFBO);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, destinationFBO);

        gl.blitFramebuffer(
            0, 0, this.width, this.height,
            0, 0, dstWidth, dstHeight,
            gl.DEPTH_BUFFER_BIT,
            gl.NEAREST
        );

        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    }

    public getColorTexture(): WebGLTexture | null {
        return this.colorTexture;
    }

    public getDepthTexture(): WebGLTexture | null {
        return this.depthTexture;
    }

    public getFramebuffer(): WebGLFramebuffer | null {
        return this.gBufferFBO;
    }

    public resize(width: number, height: number): void {
        if (this.width === width && this.height === height) return;
        this.initGBuffer(width, height);
    }

    public dispose(): void {
        const gl = this.gl;
        if (this.quadVBO) gl.deleteBuffer(this.quadVBO);
        if (this.quadVAO) gl.deleteVertexArray(this.quadVAO);
        if (this.colorTexture) gl.deleteTexture(this.colorTexture);
        if (this.depthTexture) gl.deleteTexture(this.depthTexture);
        if (this.gBufferFBO) gl.deleteFramebuffer(this.gBufferFBO);
        if (this.program) gl.deleteProgram(this.program);
    }
}
