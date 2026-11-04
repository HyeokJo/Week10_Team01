#pragma once

//<<<<<<< HEAD
//enum class ERasterizerState : uint8 { SolidBack, SolidNone, SolidFront, Wireframe, ShadowDepth, AntialiasedLine, Count };
//enum class EDepthStencilState : uint8 { Default, ReadOnly, Disabled, StencilMask, StencilOutline, Count };
//=======
enum class ERasterizerState : uint8 { SolidBack, SolidNone, SolidFront, Wireframe, ShadowDepth, SolidNoneScissor, Count };
enum class EDepthStencilState : uint8 { Default, ReadOnly, Disabled, StencilMask, StencilOutline, ReadOnlyLessEqual, DecalReceiverMask, Count };
//>>>>>>> development
enum class EBlendState : uint8 { Opaque, AlphaBlend, NoColorWrite, Count };
enum class ESamplerState : uint8 { LinearClamp, LinearWrap, Point, Comparison_Linear, Count };
