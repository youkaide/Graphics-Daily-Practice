Shader "Unlit/Day1"
{
    Properties
    {
        _MainTex ("Texture", 2D) = "white" {}
        _NormalMap ("Normal Map", 2D) = "bump" {}
        _NormalScale ("Normal Scale", Float) = 1.0
        _BaseColor ("Base Color", Color) = (1, 1, 1, 1)
        _SpecularColor ("Specular Color", Color) = (1, 1, 1, 1)
        _Shininess ("Shininess", Range(1.0, 256.0)) = 32.0

    }
    SubShader
    {
        Tags { "RenderType"="Opaque" }
        LOD 100

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            // make fog work
            #pragma multi_compile_fog

            #include "UnityCG.cginc"
            #include "Lighting.cginc"//获取光照变量

            struct appdata
            {
                float4 vertex : POSITION;
                float2 uv : TEXCOORD0;
                float3 normal : NORMAL;
                float4 tangent : TANGENT;
            };

            struct v2f
            {
                float2 uv : TEXCOORD0;
                float3 normalWS : TEXCOORD2;
                float3 tangentWS : TEXCOORD3;
                float3 bitangentWS : TEXCOORD4;
                float3 posWS : TEXCOORD5;
                UNITY_FOG_COORDS(1)
                float4 vertex : SV_POSITION;
            };

            sampler2D _MainTex;
            sampler2D _NormalMap;
            float _NormalScale;
            float4 _BaseColor;
            float4 _SpecularColor;
            float _Shininess;

            v2f vert (appdata v)
            {
                v2f o;
                o.vertex = UnityObjectToClipPos(v.vertex);
                o.uv = v.uv;
                o.posWS = mul(unity_ObjectToWorld, v.vertex).xyz;
                o.normalWS = UnityObjectToWorldNormal(v.normal);
                o.tangentWS = UnityObjectToWorldDir(v.tangent.xyz);
                // 结合切线的 w 分量（存储 UV 镜像方向）和模型奇数负缩放符号计算副切线
                float tangentSign = v.tangent.w * unity_WorldTransformParams.w;
                o.bitangentWS = cross(o.normalWS, o.tangentWS) * tangentSign;
                return o;
            }

            fixed4 frag (v2f i) : SV_Target
            {
                // 采样并解包切线空间法线（针对未压缩 RGB 法线图；若在 Unity 将贴图设为 Normal Map 格式，可改用 UnpackNormalWithScale）
                float4 packedNormal = tex2D(_NormalMap, i.uv);

                // 无需贴图：利用 UV 坐标和正弦波生成程序化波浪凹凸法线
                float3 normalTS;
                normalTS.x = sin(i.uv.x * 60.0) * 0.5 * _NormalScale;
                normalTS.y = cos(i.uv.y * 60.0) * 0.5 * _NormalScale;
                normalTS.z = sqrt(max(0.0, 1.0 - dot(normalTS.xy, normalTS.xy)));

                // float3 normalTS;
                // normalTS.xy = (packedNormal.xy * 2.0 - 1.0) * _NormalScale;
                // normalTS.z = sqrt(max(0.0, 1.0 - dot(normalTS.xy, normalTS.xy)));

                // TBN 线性组合：将切线空间法线变换到世界空间并归一化
                float3 finalNormal = normalize(normalTS.x * i.tangentWS + normalTS.y * i.bitangentWS + normalTS.z * i.normalWS);

                float3 L = normalize(_WorldSpaceLightPos0.xyz);
                float3 V = normalize(_WorldSpaceCameraPos.xyz - i.posWS);
                float3 H = normalize(V + L + 1e-5);

                // 环境光 + 漫反射 + Blinn-Phong 高光（加入 NdotL > 0 防止背光面高光漏光）
                float3 ambient = 0.05 * _BaseColor;
                float3 diffuse = _LightColor0.rgb * _BaseColor * max(0.0, dot(finalNormal, L));
                float NdotH = dot(finalNormal, H);
                float specMask = (NdotH > 0.0) ? 1.0 : 0.0;
                float3 specular = _LightColor0.rgb * _SpecularColor.rgb * specMask * pow(NdotH, max(1.0, _Shininess));
                
                return fixed4(ambient + diffuse + specular, 1.0);

            }
            ENDCG
        }
    }
}
