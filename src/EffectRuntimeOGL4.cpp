#include "Log.hpp"
#include "Runtime.hpp"
#include "Effect.hpp"
#include "EffectTree.hpp"

#include <gl\gl3w.h>
#include <vector>
#include <unordered_map>
#include <boost\algorithm\string.hpp>

// -----------------------------------------------------------------------------------------------------

#ifdef _DEBUG
	#define GLCHECK(call) { glGetError(); call; GLenum __e = glGetError(); if (__e != GL_NO_ERROR) { char __m[1024]; sprintf_s(__m, "OpenGL Error %x at line %d: %s", __e, __LINE__, #call); MessageBoxA(nullptr, __m, 0, MB_ICONERROR); } }
#else
	#define GLCHECK(call) call
#endif

namespace ReShade
{
	namespace
	{
		struct													OGL4StateBlock
		{
			OGL4StateBlock(void)
			{
				ZeroMemory(this, sizeof(this));
			}

			void												Capture(void)
			{
				GLCHECK(glGetIntegerv(GL_VIEWPORT, this->mViewport));
				GLCHECK(glGetIntegerv(GL_CURRENT_PROGRAM, reinterpret_cast<GLint *>(&this->mProgram)));
				GLCHECK(glGetIntegerv(GL_FRAMEBUFFER_BINDING, reinterpret_cast<GLint *>(&this->mFBO)));
				
				for (GLuint i = 0; i < 8; ++i)
				{
					glGetIntegerv(GL_DRAW_BUFFER0 + i, reinterpret_cast<GLint *>(&this->mDrawBuffers[i]));
				}
				
				GLCHECK(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, reinterpret_cast<GLint *>(&this->mVAO)));
				GLCHECK(this->mStencilTest = glIsEnabled(GL_STENCIL_TEST));
				GLCHECK(this->mScissorTest = glIsEnabled(GL_SCISSOR_TEST));
				GLCHECK(glGetIntegerv(GL_FRONT_FACE, reinterpret_cast<GLint *>(&this->mFrontFace)));
				GLCHECK(glGetIntegerv(GL_POLYGON_MODE, reinterpret_cast<GLint *>(&this->mPolygonMode)));
				GLCHECK(this->mCullFace = glIsEnabled(GL_CULL_FACE));
				GLCHECK(glGetIntegerv(GL_CULL_FACE_MODE, reinterpret_cast<GLint *>(&this->mCullFaceMode)));
				GLCHECK(glGetBooleanv(GL_COLOR_WRITEMASK, this->mColorMask));
				GLCHECK(this->mFramebufferSRGB = glIsEnabled(GL_FRAMEBUFFER_SRGB));
				GLCHECK(this->mBlend = glIsEnabled(GL_BLEND));
				GLCHECK(glGetIntegerv(GL_BLEND_SRC, reinterpret_cast<GLint *>(&this->mBlendFuncSrc)));
				GLCHECK(glGetIntegerv(GL_BLEND_DST, reinterpret_cast<GLint *>(&this->mBlendFuncDest)));
				GLCHECK(glGetIntegerv(GL_BLEND_EQUATION_RGB, reinterpret_cast<GLint *>(&this->mBlendEqColor)));
				GLCHECK(glGetIntegerv(GL_BLEND_EQUATION_ALPHA, reinterpret_cast<GLint *>(&this->mBlendEqAlpha)));
				GLCHECK(this->mDepthTest = glIsEnabled(GL_DEPTH_TEST));
				GLCHECK(glGetBooleanv(GL_DEPTH_WRITEMASK, &this->mDepthMask));
				GLCHECK(glGetIntegerv(GL_DEPTH_FUNC, reinterpret_cast<GLint *>(&this->mDepthFunc)));
				GLCHECK(glGetIntegerv(GL_STENCIL_VALUE_MASK, reinterpret_cast<GLint *>(&this->mStencilReadMask)));
				GLCHECK(glGetIntegerv(GL_STENCIL_WRITEMASK, reinterpret_cast<GLint *>(&this->mStencilMask)));
				GLCHECK(glGetIntegerv(GL_STENCIL_FUNC, reinterpret_cast<GLint *>(&this->mStencilFunc)));
				GLCHECK(glGetIntegerv(GL_STENCIL_FAIL, reinterpret_cast<GLint *>(&this->mStencilOpFail)));
				GLCHECK(glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, reinterpret_cast<GLint *>(&this->mStencilOpZFail)));
				GLCHECK(glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, reinterpret_cast<GLint *>(&this->mStencilOpZPass)));
				GLCHECK(glGetIntegerv(GL_STENCIL_REF, &this->mStencilRef));
				GLCHECK(glGetIntegerv(GL_ACTIVE_TEXTURE, reinterpret_cast<GLint *>(&this->mActiveTexture)));
				
				for (GLuint i = 0; i < ARRAYSIZE(this->mTextures2D); ++i)
				{
					glActiveTexture(GL_TEXTURE0 + i);
					glGetIntegerv(GL_TEXTURE_BINDING_2D, reinterpret_cast<GLint *>(&this->mTextures2D[i]));
					glGetIntegerv(GL_SAMPLER_BINDING, reinterpret_cast<GLint *>(&this->mSamplers[i]));
				}
			}
			void												Apply(void) const
			{
#define glEnableb(cap, value) if ((value)) glEnable(cap); else glDisable(cap);

				GLCHECK(glViewport(this->mViewport[0], this->mViewport[1], this->mViewport[2], this->mViewport[3]));
				GLCHECK(glUseProgram(this->mProgram));
				GLCHECK(glBindFramebuffer(GL_FRAMEBUFFER, this->mFBO));

				if (this->mDrawBuffers[1] == GL_NONE &&
					this->mDrawBuffers[2] == GL_NONE &&
					this->mDrawBuffers[3] == GL_NONE &&
					this->mDrawBuffers[4] == GL_NONE &&
					this->mDrawBuffers[5] == GL_NONE &&
					this->mDrawBuffers[6] == GL_NONE &&
					this->mDrawBuffers[7] == GL_NONE)
				{
					glDrawBuffer(this->mDrawBuffers[0]);
				}
				else
				{
					glDrawBuffers(8, this->mDrawBuffers);
				}

				GLCHECK(glBindVertexArray(this->mVAO));
				GLCHECK(glEnableb(GL_STENCIL_TEST, this->mStencilTest));
				GLCHECK(glEnableb(GL_SCISSOR_TEST, this->mScissorTest));
				GLCHECK(glFrontFace(this->mFrontFace));
				GLCHECK(glPolygonMode(GL_FRONT_AND_BACK, this->mPolygonMode));
				GLCHECK(glEnableb(GL_CULL_FACE, this->mCullFace));
				GLCHECK(glCullFace(this->mCullFaceMode));
				GLCHECK(glColorMask(this->mColorMask[0], this->mColorMask[1], this->mColorMask[2], this->mColorMask[3]));
				GLCHECK(glEnableb(GL_FRAMEBUFFER_SRGB, this->mFramebufferSRGB));
				GLCHECK(glEnableb(GL_BLEND, this->mBlend));
				GLCHECK(glBlendFunc(this->mBlendFuncSrc, this->mBlendFuncDest));
				GLCHECK(glBlendEquationSeparate(this->mBlendEqColor, this->mBlendEqAlpha));
				GLCHECK(glEnableb(GL_DEPTH_TEST, this->mDepthTest));
				GLCHECK(glDepthMask(this->mDepthMask));
				GLCHECK(glDepthFunc(this->mDepthFunc));
				GLCHECK(glStencilMask(this->mStencilMask));
				GLCHECK(glStencilFunc(this->mStencilFunc, this->mStencilRef, this->mStencilReadMask));
				GLCHECK(glStencilOp(this->mStencilOpFail, this->mStencilOpZFail, this->mStencilOpZPass));

				for (GLuint i = 0; i < ARRAYSIZE(this->mTextures2D); ++i)
				{
					GLCHECK(glActiveTexture(GL_TEXTURE0 + i));
					GLCHECK(glBindTexture(GL_TEXTURE_2D, this->mTextures2D[i]));
					GLCHECK(glBindSampler(i, this->mSamplers[i]));
				}

				GLCHECK(glActiveTexture(this->mActiveTexture));
			}

			GLint												mStencilRef, mViewport[4];
			GLuint												mStencilMask, mStencilReadMask;
			GLuint												mProgram, mFBO, mVAO, mTextures2D[8], mSamplers[8];
			GLenum												mDrawBuffers[8], mCullFace, mCullFaceMode, mPolygonMode, mBlendEqColor, mBlendEqAlpha, mBlendFuncSrc, mBlendFuncDest, mDepthFunc, mStencilFunc, mStencilOpFail, mStencilOpZFail, mStencilOpZPass, mFrontFace, mActiveTexture;
			GLboolean											mScissorTest, mBlend, mDepthTest, mDepthMask, mStencilTest, mColorMask[4], mFramebufferSRGB;
		};

		class													OGL4EffectContext : public Runtime, public std::enable_shared_from_this<OGL4EffectContext>
		{
			friend struct OGL4Effect;
			friend struct OGL4Texture;
			friend struct OGL4Constant;
			friend struct OGL4Technique;
			friend class OGL4EffectCompiler;

		public:
			OGL4EffectContext(HDC device, HGLRC context);
			~OGL4EffectContext(void);

			virtual std::unique_ptr<Effect>						CreateEffect(const EffectTree &ast, std::string &errors) const override;
			virtual void										CreateScreenshot(unsigned char *buffer, std::size_t size) const override;

		private:
			HDC													mDeviceContext;
			HGLRC												mRenderContext;
		};
		struct													OGL4Effect : public Effect
		{
			friend struct OGL4Texture;
			friend struct OGL4Sampler;
			friend struct OGL4Constant;
			friend struct OGL4Technique;

			OGL4Effect(std::shared_ptr<const OGL4EffectContext> context);
			~OGL4Effect(void);

			const Texture *										GetTexture(const std::string &name) const;
			std::vector<std::string>							GetTextureNames(void) const;
			const Constant *									GetConstant(const std::string &name) const;
			std::vector<std::string>							GetConstantNames(void) const;
			const Technique *									GetTechnique(const std::string &name) const;
			std::vector<std::string>							GetTechniqueNames(void) const;

			std::shared_ptr<const OGL4EffectContext>			mEffectContext;
			std::unordered_map<std::string, std::unique_ptr<OGL4Texture>> mTextures;
			std::vector<std::shared_ptr<OGL4Sampler>>			mSamplers;
			std::unordered_map<std::string, std::unique_ptr<OGL4Constant>> mConstants;
			std::unordered_map<std::string, std::unique_ptr<OGL4Technique>> mTechniques;
			GLuint												mDefaultVAO, mDefaultVBO, mDefaultFBO;
			std::vector<GLuint>									mUniformBuffers;
			std::vector<std::pair<unsigned char *, std::size_t>> mUniformStorages;
			mutable bool										mUniformDirty;
		};
		struct													OGL4Texture : public Effect::Texture
		{
			OGL4Texture(OGL4Effect *effect);
			~OGL4Texture(void);

			const Description									GetDescription(void) const;
			const Effect::Annotation							GetAnnotation(const std::string &name) const;

			void												Update(unsigned int level, const unsigned char *data, std::size_t size);
			void												UpdateFromColorBuffer(void);
			void												UpdateFromDepthBuffer(void);

			OGL4Effect *										mEffect;
			Description											mDesc;
			std::unordered_map<std::string, Effect::Annotation>	mAnnotations;
			GLuint												mID, mSRGBView;
		};
		struct													OGL4Sampler
		{
			OGL4Sampler(void) : mID(0)
			{
			}
			~OGL4Sampler(void)
			{
				glDeleteSamplers(1, &this->mID);
			}

			GLuint												mID;
			OGL4Texture *										mTexture;
			bool												mSRGB;
		};
		struct													OGL4Constant : public Effect::Constant
		{
			OGL4Constant(OGL4Effect *effect);
			~OGL4Constant(void);

			const Description									GetDescription(void) const;
			const Effect::Annotation							GetAnnotation(const std::string &name) const;
			void												GetValue(unsigned char *data, std::size_t size) const;
			void												SetValue(const unsigned char *data, std::size_t size);

			OGL4Effect *										mEffect;
			Description											mDesc;
			std::unordered_map<std::string, Effect::Annotation>	mAnnotations;
			std::size_t											mBuffer, mBufferOffset;
		};
		struct													OGL4Technique : public Effect::Technique
		{
			struct												Pass
			{
				GLuint											Program;
				GLuint											Framebuffer;
				GLint											StencilRef;
				GLuint											StencilMask, StencilReadMask;
				GLsizei											ViewportWidth, ViewportHeight;
				GLenum											DrawBuffers[8], BlendEqColor, BlendEqAlpha, BlendFuncSrc, BlendFuncDest, DepthFunc, StencilFunc, StencilOpFail, StencilOpZFail, StencilOpZPass;
				GLboolean										FramebufferSRGB, Blend, DepthMask, DepthTest, StencilTest, ColorMaskR, ColorMaskG, ColorMaskB, ColorMaskA;
			};

			OGL4Technique(OGL4Effect *effect);
			~OGL4Technique(void);

			const Description									GetDescription(void) const;
			const Effect::Annotation							GetAnnotation(const std::string &name) const;

			bool												Begin(unsigned int &passes) const;
			void												End(void) const;
			void												RenderPass(unsigned int index) const;

			OGL4Effect *										mEffect;
			Description											mDesc;
			std::unordered_map<std::string, Effect::Annotation>	mAnnotations;
			std::vector<Pass>									mPasses;
			mutable OGL4StateBlock								mStateblock;
		};

		class													OGL4EffectCompiler
		{
		public:
			OGL4EffectCompiler(const EffectTree &ast) : mAST(ast), mEffect(nullptr), mCurrentInParameterBlock(false), mCurrentInFunctionBlock(false), mCurrentGlobalSize(0), mCurrentGlobalStorageSize(0)
			{
			}

			bool												Traverse(OGL4Effect *effect, std::string &errors)
			{
				this->mEffect = effect;
				this->mErrors.clear();
				this->mFatal = false;
				this->mCurrentSource.clear();

				// Global uniform buffer
				this->mEffect->mUniformBuffers.push_back(0);
				this->mEffect->mUniformStorages.push_back(std::make_pair(nullptr, 0));

				const auto &root = this->mAST[EffectTree::Root].As<EffectNodes::List>();

				for (unsigned int i = 0; i < root.Length; ++i)
				{
					this->mAST[root[i]].Accept(*this);
				}

				if (this->mCurrentGlobalSize != 0)
				{
					glGenBuffers(1, &this->mEffect->mUniformBuffers[0]);

					GLint previous = 0;
					glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &previous);

					glBindBuffer(GL_UNIFORM_BUFFER, this->mEffect->mUniformBuffers[0]);
					glBufferData(GL_UNIFORM_BUFFER, this->mEffect->mUniformStorages[0].second, this->mEffect->mUniformStorages[0].first, GL_DYNAMIC_DRAW);
					
					glBindBuffer(GL_UNIFORM_BUFFER, previous);
				}

				errors += this->mErrors;

				return !this->mFatal;
			}

			static GLenum										LiteralToTextureFilter(int value)
			{
				switch (value)
				{
					default:
						return GL_NONE;
					case EffectNodes::Literal::POINT:
						return GL_NEAREST;
					case EffectNodes::Literal::LINEAR:
						return GL_LINEAR;
					case EffectNodes::Literal::ANISOTROPIC:
						return GL_LINEAR_MIPMAP_LINEAR;
				}
			}
			static GLenum										LiteralToTextureWrap(int value)
			{
				switch (value)
				{
					default:
					case EffectNodes::Literal::CLAMP:
						return GL_CLAMP_TO_EDGE;
					case EffectNodes::Literal::REPEAT:
						return GL_REPEAT;
					case EffectNodes::Literal::MIRROR:
						return GL_MIRRORED_REPEAT;
					case EffectNodes::Literal::BORDER:
						return GL_CLAMP_TO_BORDER;
				}
			}
			static GLenum										LiteralToCompFunc(int value)
			{
				switch (value)
				{
					default:
					case EffectNodes::Literal::ALWAYS:
						return GL_ALWAYS;
					case EffectNodes::Literal::NEVER:
						return GL_NEVER;
					case EffectNodes::Literal::EQUAL:
						return GL_EQUAL;
					case EffectNodes::Literal::NOTEQUAL:
						return GL_NOTEQUAL;
					case EffectNodes::Literal::LESS:
						return GL_LESS;
					case EffectNodes::Literal::LESSEQUAL:
						return GL_LEQUAL;
					case EffectNodes::Literal::GREATER:
						return GL_GREATER;
					case EffectNodes::Literal::GREATEREQUAL:
						return GL_GEQUAL;
				}
			}
			static GLenum										LiteralToStencilOp(int value)
			{
				switch (value)
				{
					default:
					case EffectNodes::Literal::KEEP:
						return GL_KEEP;
					case EffectNodes::Literal::ZERO:
						return GL_ZERO;
					case EffectNodes::Literal::REPLACE:
						return GL_REPLACE;
					case EffectNodes::Literal::INCR:
						return GL_INCR_WRAP;
					case EffectNodes::Literal::INCRSAT:
						return GL_INCR;
					case EffectNodes::Literal::DECR:
						return GL_DECR_WRAP;
					case EffectNodes::Literal::DECRSAT:
						return GL_DECR;
					case EffectNodes::Literal::INVERT:
						return GL_INVERT;
				}
			}
			static GLenum										LiteralToBlendFunc(int value)
			{
				switch (value)
				{
					default:
					case EffectNodes::Literal::ZERO:
						return GL_ZERO;
					case EffectNodes::Literal::ONE:
						return GL_ONE;
					case EffectNodes::Literal::SRCCOLOR:
						return GL_SRC_COLOR;
					case EffectNodes::Literal::SRCALPHA:
						return GL_SRC_ALPHA;
					case EffectNodes::Literal::INVSRCCOLOR:
						return GL_ONE_MINUS_SRC_COLOR;
					case EffectNodes::Literal::INVSRCALPHA:
						return GL_ONE_MINUS_SRC_ALPHA;
					case EffectNodes::Literal::DESTCOLOR:
						return GL_DST_COLOR;
					case EffectNodes::Literal::DESTALPHA:
						return GL_DST_ALPHA;
					case EffectNodes::Literal::INVDESTCOLOR:
						return GL_ONE_MINUS_DST_COLOR;
					case EffectNodes::Literal::INVDESTALPHA:
						return GL_ONE_MINUS_DST_ALPHA;
				}
			}
			static GLenum										LiteralToBlendEq(int value)
			{
				switch (value)
				{
					default:
					case EffectNodes::Literal::ADD:
						return GL_FUNC_ADD;
					case EffectNodes::Literal::SUBTRACT:
						return GL_FUNC_SUBTRACT;
					case EffectNodes::Literal::REVSUBTRACT:
						return GL_FUNC_REVERSE_SUBTRACT;
					case EffectNodes::Literal::MIN:
						return GL_MIN;
					case EffectNodes::Literal::MAX:
						return GL_MAX;
				}
			}
			static void											LiteralToFormat(int value, GLenum &internalformat, GLenum &internalformatsrgb, Effect::Texture::Format &format)
			{
				switch (value)
				{
					case EffectNodes::Literal::R8:
						format = Effect::Texture::Format::R8;
						internalformat = internalformatsrgb = GL_R8;
						break;
					case EffectNodes::Literal::R32F:
						format = Effect::Texture::Format::R32F;
						internalformat = internalformatsrgb = GL_R32F;
						break;
					case EffectNodes::Literal::RG8:
						format = Effect::Texture::Format::RG8;
						internalformat = internalformatsrgb = GL_RG8;
						break;
					case EffectNodes::Literal::RGBA8:
						format = Effect::Texture::Format::RGBA8;
						internalformat = GL_RGBA8;
						internalformatsrgb = GL_SRGB8_ALPHA8;
						break;
					case EffectNodes::Literal::RGBA16:
						format = Effect::Texture::Format::RGBA16;
						internalformat = internalformatsrgb = GL_RGBA16;
						break;
					case EffectNodes::Literal::RGBA16F:
						format = Effect::Texture::Format::RGBA16F;
						internalformat = internalformatsrgb = GL_RGBA16F;
						break;
					case EffectNodes::Literal::RGBA32F:
						format = Effect::Texture::Format::RGBA32F;
						internalformat = internalformatsrgb = GL_RGBA32F;
						break;
					case EffectNodes::Literal::DXT1:
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
						format = Effect::Texture::Format::DXT1;
						internalformat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
						internalformatsrgb = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
						break;
					case EffectNodes::Literal::DXT3:
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT 0x8C4E
						format = Effect::Texture::Format::DXT3;
						internalformat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
						internalformatsrgb = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT;
						break;
					case EffectNodes::Literal::DXT5:
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
						format = Effect::Texture::Format::DXT5;
						internalformat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
						internalformatsrgb = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
						break;
					case EffectNodes::Literal::LATC1:
#define GL_COMPRESSED_LUMINANCE_LATC1_EXT 0x8C70
						format = Effect::Texture::Format::LATC1;
						internalformat = internalformatsrgb = GL_COMPRESSED_LUMINANCE_LATC1_EXT;
						break;
					case EffectNodes::Literal::LATC2:
#define GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT 0x8C72
						format = Effect::Texture::Format::LATC2;
						internalformat = internalformatsrgb = GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT;
						break;
					default:
						format = Effect::Texture::Format::Unknown;
						internalformat = internalformatsrgb = GL_NONE;
						break;
				}
			}

			static inline std::string							PrintLocation(const EffectTree::Location &location)
			{
				return std::string(location.Source != nullptr ? location.Source : "") + "(" + std::to_string(location.Line) + ", " + std::to_string(location.Column) + "): ";
			}

			static std::string									FixName(const std::string &name)
			{
				std::string res;

				if (boost::starts_with(name, "gl_") ||
					name == "common" || name == "partition" || name == "input" || name == "ouput" || name == "active" || name == "filter" || name == "superp" ||
					name == "invariant" || name == "lowp" || name == "mediump" || name == "highp" || name == "precision" || name == "patch" || name == "subroutine" ||
					name == "abs" || name == "sign" || name == "all" || name == "any" || name == "sin" || name == "sinh" || name == "cos" || name == "cosh" || name == "tan" || name == "tanh" || name == "asin" || name == "acos" || name == "atan" || name == "exp" || name == "exp2" || name == "log" || name == "log2" || name == "sqrt" || name == "inversesqrt" || name == "ceil" || name == "floor" || name == "fract" || name == "trunc" || name == "round" || name == "radians" || name == "degrees" || name == "length" || name == "normalize" || name == "transpose" || name == "determinant" || name == "intBitsToFloat" || name == "uintBitsToFloat" || name == "floatBitsToInt" || name == "floatBitsToUint" || name == "matrixCompMult" || name == "not" || name == "lessThan" || name == "greaterThan" || name == "lessThanEqual" || name == "greaterThanEqual" || name == "equal" || name == "notEqual" || name == "dot" || name == "cross" || name == "distance" || name == "pow" || name == "modf" || name == "frexp" || name == "ldexp" || name == "min" || name == "max" || name == "step" || name == "reflect" || name == "texture" || name == "textureOffset" || name == "fma" || name == "mix" || name == "clamp" || name == "smoothstep" || name == "refract" || name == "faceforward" || name == "textureLod" || name == "textureLodOffset" || name == "texelFetch" || name == "main")
				{
					res += '_';
				}

				res += name;

				return res;
			}
			static std::string									FixNameWithSemantic(const std::string &name, const std::string &semantic)
			{
				if (semantic == "SV_VERTEXID")
				{
					return "glVertexID";
				}
				else if (semantic == "SV_POSITION")
				{
					return "glPosition";
				}

				return FixName(name);
			}
			std::string											PrintType(const EffectNodes::Type &type)
			{
				switch (type.Class)
				{
					default:
						return "";
					case EffectNodes::Type::Void:
						return "void";
					case EffectNodes::Type::Bool:
						if (type.IsMatrix())
							return "mat" + std::to_string(type.Rows) + "x" + std::to_string(type.Cols);
						else if (type.IsVector())
							return "bvec" + std::to_string(type.Rows);
						else
							return "bool";
					case EffectNodes::Type::Int:
						if (type.IsMatrix())
							return "mat" + std::to_string(type.Rows) + "x" + std::to_string(type.Cols);
						else if (type.IsVector())
							return "ivec" + std::to_string(type.Rows);
						else
							return "int";
					case EffectNodes::Type::Uint:
						if (type.IsMatrix())
							return "mat" + std::to_string(type.Rows) + "x" + std::to_string(type.Cols);
						else if (type.IsVector())
							return "uvec" + std::to_string(type.Rows);
						else
							return "uint";
					case EffectNodes::Type::Float:
						if (type.IsMatrix())
							return "mat" + std::to_string(type.Rows) + "x" + std::to_string(type.Cols);
						else if (type.IsVector())
							return "vec" + std::to_string(type.Rows);
						else
							return "float";
					case EffectNodes::Type::Sampler:
						return "sampler2D";
					case EffectNodes::Type::Struct:
						return FixName(this->mAST[type.Definition].As<EffectNodes::Struct>().Name);
				}
			}
			std::string											PrintTypeWithQualifiers(const EffectNodes::Type &type)
			{
				std::string qualifiers;

				if (type.HasQualifier(EffectNodes::Type::Qualifier::NoInterpolation))
					qualifiers += "flat ";
				if (type.HasQualifier(EffectNodes::Type::Qualifier::NoPerspective))
					qualifiers += "noperspective ";
				if (type.HasQualifier(EffectNodes::Type::Qualifier::Linear))
					qualifiers += "smooth ";
				if (type.HasQualifier(EffectNodes::Type::Qualifier::Sample))
					qualifiers += "sample ";
				if (type.HasQualifier(EffectNodes::Type::Qualifier::Centroid))
					qualifiers += "centroid ";
				if (type.HasQualifier(EffectNodes::Type::Qualifier::InOut))
					qualifiers += "inout ";
				else if (type.HasQualifier(EffectNodes::Type::Qualifier::In))
					qualifiers += "in ";
				else if (type.HasQualifier(EffectNodes::Type::Qualifier::Out))
					qualifiers += "out ";
				else if (type.HasQualifier(EffectNodes::Type::Qualifier::Uniform))
					qualifiers += "uniform ";
				if (type.HasQualifier(EffectNodes::Type::Qualifier::Const))
					qualifiers += "const ";

				return qualifiers + PrintType(type);
			}
			std::pair<std::string, std::string>					PrintCast(const EffectNodes::Type &from, const EffectNodes::Type &to)
			{
				std::pair<std::string, std::string> code;

				if (from.Class != to.Class && !(from.IsMatrix() && to.IsMatrix()))
				{
					const EffectNodes::Type type = { to.Class, 0, from.Rows, from.Cols };

					code.first += PrintType(type) + "(";
					code.second += ")";
				}

				if (from.Rows > 0 && from.Rows < to.Rows)
				{
					const char subscript[4] = { 'x', 'y', 'z', 'w' };

					code.second += '.';

					for (unsigned int i = 0; i < from.Rows; ++i)
					{
						code.second += subscript[i];
					}
					for (unsigned int i = from.Rows; i < to.Rows; ++i)
					{
						code.second += subscript[from.Rows - 1];
					}
				}
				else if (from.Rows > to.Rows)
				{
					const char subscript[4] = { 'x', 'y', 'z', 'w' };

					code.second += '.';

					for (unsigned int i = 0; i < to.Rows; ++i)
					{
						code.second += subscript[i];
					}
				}

				return code;
			}

			void												Visit(const EffectNodes::LValue &node)
			{
				this->mCurrentSource += FixName(this->mAST[node.Reference].As<EffectNodes::Variable>().Name);
			}
			void												Visit(const EffectNodes::Literal &node)
			{
				if (!node.Type.IsScalar())
				{
					this->mCurrentSource += PrintType(node.Type);
					this->mCurrentSource += '(';
				}

				for (unsigned int i = 0; i < node.Type.Rows * node.Type.Cols; ++i)
				{
					switch (node.Type.Class)
					{
						case EffectNodes::Type::Bool:
							this->mCurrentSource += node.Value.Bool[i] ? "true" : "false";
							break;
						case EffectNodes::Type::Int:
							this->mCurrentSource += std::to_string(node.Value.Int[i]);
							break;
						case EffectNodes::Type::Uint:
							this->mCurrentSource += std::to_string(node.Value.Uint[i]) + "u";
							break;
						case EffectNodes::Type::Float:
							this->mCurrentSource += std::to_string(node.Value.Float[i]);
							break;
					}

					this->mCurrentSource += ", ";
				}

				this->mCurrentSource.pop_back();
				this->mCurrentSource.pop_back();

				if (!node.Type.IsScalar())
				{
					this->mCurrentSource += ')';
				}
			}
			void												Visit(const EffectNodes::Expression &node)
			{
				std::string part1, part2, part3, part4;
				std::pair<std::string, std::string> cast1, cast2, cast3, cast121, cast122;
				EffectNodes::Type type1, type2, type3, type12;

				cast1 = PrintCast(type1 = this->mAST[node.Operands[0]].As<EffectNodes::RValue>().Type, node.Type);

				if (node.Operands[1] != 0)
				{
					cast2 = PrintCast(type2 = this->mAST[node.Operands[1]].As<EffectNodes::RValue>().Type, node.Type);

					type12 = type2.IsFloatingPoint() ? type2 : type1;
					type12.Rows = std::max(type1.Rows, type2.Rows);
					type12.Cols = std::max(type1.Cols, type2.Cols);
					cast121 = PrintCast(type1, type12), cast122 = PrintCast(type2, type12);
				}
				if (node.Operands[2] != 0)
				{
					cast3 = PrintCast(type3 = this->mAST[node.Operands[1]].As<EffectNodes::RValue>().Type, node.Type);
				}

				switch (node.Operator)
				{
					case EffectNodes::Expression::Negate:
						part1 = '-';
						break;
					case EffectNodes::Expression::BitNot:
						part1 = '~';
						break;
					case EffectNodes::Expression::LogicNot:
					{
						if (node.Type.IsVector())
						{
							part1 = "not(" + cast1.first;
							part2 = cast1.second + ')';
						}
						else
						{
							part1 = '!';
						}
						break;
					}
					case EffectNodes::Expression::Increase:
						part1 = "++";
						break;
					case EffectNodes::Expression::Decrease:
						part1 = "--";
						break;
					case EffectNodes::Expression::PostIncrease:
						part2 = "++";
						break;
					case EffectNodes::Expression::PostDecrease:
						part2 = "--";
						break;
					case EffectNodes::Expression::Abs:
						part1 = "abs(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Sign:
						part1 = cast1.first + "sign(";
						part2 = ')' + cast1.second;
						break;
					case EffectNodes::Expression::Rcp:
						part1 = '(' + PrintType(node.Type) + "(1.0) / ";
						part2 = ')';
						break;
					case EffectNodes::Expression::All:
					{
						if (type1.IsVector())
						{
							part1 = "all(bvec" + std::to_string(type1.Rows) + '(';
							part2 = "))";
						}
						else
						{
							part1 = "bool(";
							part2 = ')';
						}
						break;
					}
					case EffectNodes::Expression::Any:
					{
						if (type1.IsVector())
						{
							part1 = "any(bvec" + std::to_string(type1.Rows) + '(';
							part2 = "))";
						}
						else
						{
							part1 = "bool(";
							part2 = ')';
						}
						break;
					}
					case EffectNodes::Expression::Sin:
						part1 = "sin(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Sinh:
						part1 = "sinh(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Cos:
						part1 = "cos(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Cosh:
						part1 = "cosh(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Tan:
						part1 = "tan(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Tanh:
						part1 = "tanh(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Asin:
						part1 = "asin(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Acos:
						part1 = "acos(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Atan:
						part1 = "atan(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Exp:
						part1 = "exp(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Exp2:
						part1 = "exp2(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Log:
						part1 = "log(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Log2:
						part1 = "log2(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Log10:
						part1 = "(log2(" + cast1.first;
						part2 = cast1.second + ") / " + PrintType(node.Type) + "(2.302585093))";
						break;
					case EffectNodes::Expression::Sqrt:
						part1 = "sqrt(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Rsqrt:
						part1 = "inversesqrt(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Ceil:
						part1 = "ceil(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Floor:
						part1 = "floor(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Frac:
						part1 = "fract(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Trunc:
						part1 = "trunc(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Round:
						part1 = "round(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Saturate:
						part1 = "clamp(" + cast1.first;
						part2 = cast1.second + ", 0.0, 1.0)";
						break;
					case EffectNodes::Expression::Radians:
						part1 = "radians(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Degrees:
						part1 = "degrees(" + cast1.first;
						part2 = cast1.second + ')';
						break;
					case EffectNodes::Expression::Noise:
					{
						part1 = "noise1(";

						if (!type1.IsFloatingPoint())
						{
							type1.Class = EffectNodes::Type::Float;
							part1 += PrintType(type1) + '(';
							part2 = ')';
						}

						part2 += ')';
						break;
					}
					case EffectNodes::Expression::Length:
					{
						part1 = "length(";

						if (!type1.IsFloatingPoint())
						{
							type1.Class = EffectNodes::Type::Float;
							part1 += PrintType(type1) + '(';
							part2 = ')';
						}

						part2 += ')';
						break;
					}
					case EffectNodes::Expression::Normalize:
					{
						part1 = "normalize(";

						if (!type1.IsFloatingPoint())
						{
							type1.Class = EffectNodes::Type::Float;
							part1 += PrintType(type1) + '(';
							part2 = ')';
						}

						part2 += ')';
						break;
					}
					case EffectNodes::Expression::Transpose:
					{
						part1 = "transpose(";

						if (!type1.IsFloatingPoint())
						{
							type1.Class = EffectNodes::Type::Float;
							part1 += PrintType(type1) + '(';
							part2 = ')';
						}

						part2 += ')';
						break;
					}
					case EffectNodes::Expression::Determinant:
					{
						part1 = "determinant(";

						if (!type1.IsFloatingPoint())
						{
							type1.Class = EffectNodes::Type::Float;
							part1 += PrintType(type1) + '(';
							part2 = ')';
						}

						part2 += ')';
						break;
					}
					case EffectNodes::Expression::Cast:
						part1 = PrintType(node.Type) + '(';
						part2 = ')';
						break;
					case EffectNodes::Expression::BitCastInt2Float:
					{
						part1 = "intBitsToFloat(";

						if (type1.Class != EffectNodes::Type::Int)
						{
							type1.Class = EffectNodes::Type::Int;
							part1 += PrintType(type1) + '(';
							part2 = ')';
						}

						part2 += ')';
						break;
					}
					case EffectNodes::Expression::BitCastUint2Float:
					{
						part1 = "uintBitsToFloat(";

						if (type1.Class != EffectNodes::Type::Uint)
						{
							type1.Class = EffectNodes::Type::Uint;
							part1 += PrintType(type1) + '(';
							part2 = ')';
						}

						part2 += ')';
						break;
					}
					case EffectNodes::Expression::BitCastFloat2Int:
					{
						part1 = "floatBitsToInt(";

						if (type1.Class != EffectNodes::Type::Float)
						{
							type1.Class = EffectNodes::Type::Float;
							part1 += PrintType(type1) + '(';
							part2 = ')';
						}

						part2 += ')';
						break;
					}
					case EffectNodes::Expression::BitCastFloat2Uint:
					{
						part1 = "floatBitsToUint(";

						if (type1.Class != EffectNodes::Type::Float)
						{
							type1.Class = EffectNodes::Type::Float;
							part1 += PrintType(type1) + '(';
							part2 = ')';
						}

						part2 += ')';
						break;
					}
					case EffectNodes::Expression::Add:
						part1 = '(' + cast1.first;
						part2 = cast1.second + " + " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Subtract:
						part1 = '(' + cast1.first;
						part2 = cast1.second + " - " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Multiply:
						if (node.Type.IsMatrix())
						{
							part1 = "matrixCompMult(" + cast1.first;
							part2 = cast1.second + ", " + cast2.first;
							part3 = cast2.second + ')';
						}
						else
						{
							part1 = '(' + cast1.first;
							part2 = cast1.second + " * " + cast2.first;
							part3 = cast2.second + ')';
						}
						break;
					case EffectNodes::Expression::Divide:
						part1 = '(' + cast1.first;
						part2 = cast1.second + " / " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Modulo:
						if (node.Type.IsFloatingPoint())
						{
							part1 = "_fmod(" + cast1.first;
							part2 = cast1.second + ", " + cast2.first;
							part3 = cast2.second + ')';
						}
						else
						{
							part1 = '(' + cast1.first;
							part2 = cast1.second + " % " + cast2.first;
							part3 = cast2.second + ')';
						}
						break;
					case EffectNodes::Expression::Less:
						if (node.Type.IsVector())
						{
							part1 = "lessThan(" + cast121.first;
							part2 = cast121.second + ", " + cast122.first;
							part3 = cast122.second + ')';
						}
						else
						{
							part1 = '(' + cast121.first;
							part2 = cast121.second + " < " + cast122.first;
							part3 = cast122.second + ')';
						}
						break;
					case EffectNodes::Expression::Greater:
						if (node.Type.IsVector())
						{
							part1 = "greaterThan(" + cast121.first;
							part2 = cast121.second + ", " + cast122.first;
							part3 = cast122.second + ')';
						}
						else
						{
							part1 = '(' + cast121.first;
							part2 = cast121.second + " > " + cast122.first;
							part3 = cast122.second + ')';
						}
						break;
					case EffectNodes::Expression::LessOrEqual:
						if (node.Type.IsVector())
						{
							part1 = "lessThanEqual(" + cast121.first;
							part2 = cast121.second + ", " + cast122.first;
							part3 = cast122.second + ')';
						}
						else
						{
							part1 = '(' + cast121.first;
							part2 = cast121.second + " <= " + cast122.first;
							part3 = cast122.second + ')';
						}
						break;
					case EffectNodes::Expression::GreaterOrEqual:
						if (node.Type.IsVector())
						{
							part1 = "greaterThanEqual(" + cast121.first;
							part2 = cast121.second + ", " + cast122.first;
							part3 = cast122.second + ')';
						}
						else
						{
							part1 = '(' + cast121.first;
							part2 = cast121.second + " >= " + cast122.first;
							part3 = cast122.second + ')';
						}
						break;
					case EffectNodes::Expression::Equal:
						if (node.Type.IsVector())
						{
							part1 = "equal(" + cast121.first;
							part2 = cast121.second + ", " + cast122.first;
							part3 = cast122.second + ")";
						}
						else
						{
							part1 = '(' + cast121.first;
							part2 = cast121.second + " == " + cast122.first;
							part3 = cast122.second + ')';
						}
						break;
					case EffectNodes::Expression::NotEqual:
						if (node.Type.IsVector())
						{
							part1 = "notEqual(" + cast121.first;
							part2 = cast121.second + ", " + cast122.first;
							part3 = cast122.second + ")";
						}
						else
						{
							part1 = '(' + cast121.first;
							part2 = cast121.second + " != " + cast122.first;
							part3 = cast122.second + ')';
						}
						break;
					case EffectNodes::Expression::LeftShift:
						part1 = '(';
						part2 = " << ";
						part3 = ')';
						break;
					case EffectNodes::Expression::RightShift:
						part1 = '(';
						part2 = " >> ";
						part3 = ')';
						break;
					case EffectNodes::Expression::BitAnd:
						part1 = '(' + cast1.first;
						part2 = cast1.second + " & " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::BitXor:
						part1 = '(' + cast1.first;
						part2 = cast1.second + " ^ " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::BitOr:
						part1 = '(' + cast1.first;
						part2 = cast1.second + " | " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::LogicAnd:
						part1 = '(' + cast121.first;
						part2 = cast121.second + " && " + cast122.first;
						part3 = cast122.second + ')';
						break;
					case EffectNodes::Expression::LogicXor:
						part1 = '(' + cast121.first;
						part2 = cast121.second + " ^^ " + cast122.first;
						part3 = cast122.second + ')';
						break;
					case EffectNodes::Expression::LogicOr:
						part1 = '(' + cast121.first;
						part2 = cast121.second + " || " + cast122.first;
						part3 = cast122.second + ')';
						break;
					case EffectNodes::Expression::Mul:
						part1 = '(';
						part2 = " * ";
						part3 = ')';
						break;
					case EffectNodes::Expression::Atan2:
						part1 = "atan(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Dot:
						part1 = "dot(" + cast121.first;
						part2 = cast121.second + ", " + cast122.first;
						part3 = cast122.second + ')';
						break;
					case EffectNodes::Expression::Cross:
						part1 = "cross(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Distance:
						part1 = "distance(" + cast121.first;
						part2 = cast121.second + ", " + cast122.first;
						part3 = cast122.second + ')';
						break;
					case EffectNodes::Expression::Pow:
						part1 = "pow(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Modf:
						part1 = "modf(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Frexp:
						part1 = "frexp(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Ldexp:
						part1 = "ldexp(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Min:
						part1 = "min(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Max:
						part1 = "max(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Step:
						part1 = "step(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Reflect:
						part1 = "reflect(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					case EffectNodes::Expression::Extract:
						part2 = '[';
						part3 = ']';
						break;
					case EffectNodes::Expression::Field:
						this->mCurrentSource += '(';
						this->mAST[node.Operands[0]].Accept(*this);
						this->mCurrentSource += (this->mAST[node.Operands[0]].Is<EffectNodes::LValue>() && this->mAST[node.Operands[0]].As<EffectNodes::LValue>().Type.HasQualifier(EffectNodes::Type::Uniform)) ? '_' : '.';
						this->mCurrentSource += this->mAST[node.Operands[1]].As<EffectNodes::Variable>().Name;
						this->mCurrentSource += ')';
						return;
					case EffectNodes::Expression::Tex:
					{
						part1 = "texture(";
						const EffectNodes::Type type2to = { EffectNodes::Type::Float, 0, 2, 1 };
						cast2 = PrintCast(type2, type2to);
						part2 = ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					}
					case EffectNodes::Expression::TexLevel:
					{
						part1 = "_textureLod(";
						const EffectNodes::Type type2to = { EffectNodes::Type::Float, 0, 4, 1 };
						cast2 = PrintCast(type2, type2to);
						part2 = ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					}
					case EffectNodes::Expression::TexGather:
					{
						part1 = "textureGather(";
						const EffectNodes::Type type2to = { EffectNodes::Type::Float, 0, 2, 1 };
						cast2 = PrintCast(type2, type2to);
						part2 = ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					}
					case EffectNodes::Expression::TexBias:
					{
						part1 = "_textureBias(";
						const EffectNodes::Type type2to = { EffectNodes::Type::Float, 0, 4, 1 };
						cast2 = PrintCast(type2, type2to);
						part2 = ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					}
					case EffectNodes::Expression::TexFetch:
					{
						part1 = "texelFetch(";
						const EffectNodes::Type type2to = { EffectNodes::Type::Int, 0, 2, 1 };
						cast2 = PrintCast(type2, type2to);
						part2 = ", " + cast2.first;
						part3 = cast2.second + ')';
						break;
					}
					case EffectNodes::Expression::TexSize:
						part1 = "textureSize(";
						part2 = ", int(";
						part3 = "))";
						break;
					case EffectNodes::Expression::Mad:
						part1 = "fma(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ", " + cast3.first;
						part4 = cast3.second + ')';
						break;
					case EffectNodes::Expression::SinCos:
					{
						part1 = "_sincos(";

						if (type1.Class != EffectNodes::Type::Float)
						{
							type1.Class = EffectNodes::Type::Float;
							part1 += PrintType(type1) + '(';
							part2 = ')';
						}

						part2 = ", ";
						part3 = ", ";
						part4 = ')';
						break;
					}
					case EffectNodes::Expression::Lerp:
						part1 = "mix(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ", " + cast3.first;
						part4 = cast3.second + ')';
						break;
					case EffectNodes::Expression::Clamp:
						part1 = "clamp(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ", " + cast3.first;
						part4 = cast3.second + ')';
						break;
					case EffectNodes::Expression::SmoothStep:
						part1 = "smoothstep(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ", " + cast3.first;
						part4 = cast3.second + ')';
						break;
					case EffectNodes::Expression::Refract:
						part1 = "refract(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ", float";
						part4 = "))";
						break;
					case EffectNodes::Expression::FaceForward:
						part1 = "faceforward(" + cast1.first;
						part2 = cast1.second + ", " + cast2.first;
						part3 = cast2.second + ", " + cast3.first;
						part4 = cast3.second + ')';
						break;
					case EffectNodes::Expression::Conditional:
					{
						part1 = '(';

						if (this->mAST[node.Operands[0]].As<EffectNodes::RValue>().Type.IsVector())
						{
							part1 += "all(bvec" + std::to_string(this->mAST[node.Operands[0]].As<EffectNodes::RValue>().Type.Rows) + '(';
							part2 = "))";
						}
						else
						{
							part1 += "bool(";
							part2 = ')';
						}

						part2 += " ? " + cast2.first;
						part3 = cast2.second + " : " + cast3.first;
						part4 = cast3.second + ')';
						break;
					}
					case EffectNodes::Expression::TexOffset:
					{
						part1 = "textureOffset(";
						const EffectNodes::Type type2to = { EffectNodes::Type::Float, 0, 2, 1 };
						const EffectNodes::Type type3to = { EffectNodes::Type::Int, 0, 2, 1 };
						cast2 = PrintCast(type2, type2to);
						part2 = ", " + cast2.first;
						cast3 = PrintCast(type3, type3to);
						part3 = cast2.second + ", " + cast3.first;
						part4 = cast3.second + ')';
						break;
					}
					case EffectNodes::Expression::TexLevelOffset:
					{	
						part1 = "_textureLodOffset(";
						const EffectNodes::Type type2to = { EffectNodes::Type::Float, 0, 4, 1 };
						const EffectNodes::Type type3to = { EffectNodes::Type::Int, 0, 2, 1 };
						cast2 = PrintCast(type2, type2to);
						part2 = ", " + cast2.first;
						cast3 = PrintCast(type3, type3to);
						part3 = cast2.second + ", " + cast3.first;
						part4 = cast3.second + ')';
						break;
					}
					case EffectNodes::Expression::TexGatherOffset:
					{
						part1 = "textureGatherOffset(";
						const EffectNodes::Type type2to = { EffectNodes::Type::Float, 0, 2, 1 };
						const EffectNodes::Type type3to = { EffectNodes::Type::Int, 0, 2, 1 };
						cast2 = PrintCast(type2, type2to);
						part2 = ", " + cast2.first;
						cast3 = PrintCast(type3, type3to);
						part3 = cast2.second + ", " + cast3.first;
						part4 = cast3.second + ')';
						break;
					}
				}

				this->mCurrentSource += part1;
				this->mAST[node.Operands[0]].Accept(*this);
				this->mCurrentSource += part2;

				if (node.Operands[1] != 0)
				{
					this->mAST[node.Operands[1]].Accept(*this);
				}

				this->mCurrentSource += part3;

				if (node.Operands[2] != 0)
				{
					this->mAST[node.Operands[2]].Accept(*this);
				}

				this->mCurrentSource += part4;
			}
			void												Visit(const EffectNodes::Sequence &node)
			{
				for (unsigned int i = 0; i < node.Length; ++i)
				{
					this->mAST[node[i]].Accept(*this);

					this->mCurrentSource += ", ";
				}

				this->mCurrentSource.pop_back();
				this->mCurrentSource.pop_back();
			}
			void												Visit(const EffectNodes::Assignment &node)
			{
				this->mCurrentSource += '(';
				this->mAST[node.Left].Accept(*this);
				this->mCurrentSource += ' ';

				switch (node.Operator)
				{
					case EffectNodes::Expression::None:
						this->mCurrentSource += '=';
						break;
					case EffectNodes::Expression::Add:
						this->mCurrentSource += "+=";
						break;
					case EffectNodes::Expression::Subtract:
						this->mCurrentSource += "-=";
						break;
					case EffectNodes::Expression::Multiply:
						this->mCurrentSource += "*=";
						break;
					case EffectNodes::Expression::Divide:
						this->mCurrentSource += "/=";
						break;
					case EffectNodes::Expression::Modulo:
						this->mCurrentSource += "%=";
						break;
					case EffectNodes::Expression::LeftShift:
						this->mCurrentSource += "<<=";
						break;
					case EffectNodes::Expression::RightShift:
						this->mCurrentSource += ">>=";
						break;
					case EffectNodes::Expression::BitAnd:
						this->mCurrentSource += "&=";
						break;
					case EffectNodes::Expression::BitXor:
						this->mCurrentSource += "^=";
						break;
					case EffectNodes::Expression::BitOr:
						this->mCurrentSource += "|=";
						break;
				}

				const std::pair<std::string, std::string> cast = PrintCast(this->mAST[node.Right].As<EffectNodes::RValue>().Type, this->mAST[node.Left].As<EffectNodes::RValue>().Type);

				this->mCurrentSource += ' ';
				this->mCurrentSource += cast.first;
				this->mAST[node.Right].Accept(*this);
				this->mCurrentSource += cast.second;
				this->mCurrentSource += ')';
			}
			void												Visit(const EffectNodes::Call &node)
			{
				this->mCurrentSource += node.CalleeName;
				this->mCurrentSource += '(';

				if (node.HasArguments())
				{
					const auto &arguments = this->mAST[node.Arguments].As<EffectNodes::List>();
					const auto &parameters = this->mAST[this->mAST[node.Callee].As<EffectNodes::Function>().Parameters].As<EffectNodes::List>();

					for (unsigned int i = 0; i < arguments.Length; ++i)
					{
						const auto &argument = this->mAST[arguments[i]].As<EffectNodes::RValue>();
						const auto &parameter = this->mAST[parameters[i]].As<EffectNodes::Variable>();
						
						const std::pair<std::string , std::string> cast = PrintCast(argument.Type, parameter.Type);

						this->mCurrentSource += cast.first;
						argument.Accept(*this);
						this->mCurrentSource += cast.second;
						this->mCurrentSource += ", ";
					}

					this->mCurrentSource.pop_back();
					this->mCurrentSource.pop_back();
				}

				this->mCurrentSource += ')';
			}
			void												Visit(const EffectNodes::Constructor &node)
			{
				if (node.Type.IsMatrix())
				{
					this->mCurrentSource += "transpose(";
				}

				this->mCurrentSource += PrintType(node.Type);
				this->mCurrentSource += '(';

				const auto &arguments = this->mAST[node.Arguments].As<EffectNodes::List>();

				for (unsigned int i = 0; i < arguments.Length; ++i)
				{
					this->mAST[arguments[i]].Accept(*this);

					this->mCurrentSource += ", ";
				}

				this->mCurrentSource.pop_back();
				this->mCurrentSource.pop_back();

				this->mCurrentSource += ')';

				if (node.Type.IsMatrix())
				{
					this->mCurrentSource += ')';
				}
			}
			void												Visit(const EffectNodes::Swizzle &node)
			{
				const EffectNodes::RValue &left = this->mAST[node.Operands[0]].As<EffectNodes::RValue>();

				left.Accept(*this);

				this->mCurrentSource += '.';

				if (left.Type.IsMatrix())
				{
					const char swizzle[16][5] =
					{
						"_m00", "_m01", "_m02", "_m03",
						"_m10", "_m11", "_m12", "_m13",
						"_m20", "_m21", "_m22", "_m23",
						"_m30", "_m31", "_m32", "_m33"
					};

					for (int i = 0; i < 4 && node.Mask[i] >= 0; ++i)
					{
						this->mCurrentSource += swizzle[node.Mask[i]];
					}
				}
				else
				{
					const char swizzle[4] =
					{
						'x', 'y', 'z', 'w'
					};

					for (int i = 0; i < 4 && node.Mask[i] >= 0; ++i)
					{
						this->mCurrentSource += swizzle[node.Mask[i]];
					}
				}
			}
			void												Visit(const EffectNodes::If &node)
			{
				this->mCurrentSource += "if (";
				this->mAST[node.Condition].Accept(*this);
				this->mCurrentSource += ")\n";

				if (node.StatementOnTrue != 0)
				{
					this->mAST[node.StatementOnTrue].Accept(*this);
				}
				else
				{
					this->mCurrentSource += "\t;";
				}
				if (node.StatementOnFalse != 0)
				{
					this->mCurrentSource += "else\n";
					this->mAST[node.StatementOnFalse].Accept(*this);
				}
			}
			void												Visit(const EffectNodes::Switch &node)
			{
				this->mCurrentSource += "switch (";
				this->mAST[node.Test].Accept(*this);
				this->mCurrentSource += ")\n{\n";

				const auto &cases = this->mAST[node.Cases].As<EffectNodes::List>();

				for (unsigned int i = 0; i < cases.Length; ++i)
				{
					this->mAST[cases[i]].As<EffectNodes::Case>().Accept(*this);
				}

				this->mCurrentSource += "}\n";
			}
			void												Visit(const EffectNodes::Case &node)
			{
				const auto &labels = this->mAST[node.Labels].As<EffectNodes::List>();

				for (unsigned int i = 0; i < labels.Length; ++i)
				{
					const auto &label = this->mAST[labels[i]];

					if (label.Is<EffectNodes::Expression>())
					{
						this->mCurrentSource += "default";
					}
					else
					{
						this->mCurrentSource += "case ";
						label.As<EffectNodes::Literal>().Accept(*this);
					}

					this->mCurrentSource += ":\n";
				}

				this->mAST[node.Statements].As<EffectNodes::StatementBlock>().Accept(*this);
			}
			void												Visit(const EffectNodes::For &node)
			{
				this->mCurrentSource += "for (";

				if (node.Initialization != 0)
				{
					this->mAST[node.Initialization].Accept(*this);

					this->mCurrentSource.pop_back();
					this->mCurrentSource.pop_back();
				}

				this->mCurrentSource += "; ";
										
				if (node.Condition != 0)
				{
					this->mAST[node.Condition].Accept(*this);
				}

				this->mCurrentSource += "; ";

				if (node.Iteration != 0)
				{
					this->mAST[node.Iteration].Accept(*this);
				}

				this->mCurrentSource += ")\n";

				if (node.Statements != 0)
				{
					this->mAST[node.Statements].Accept(*this);
				}
				else
				{
					this->mCurrentSource += "\t;";
				}
			}
			void												Visit(const EffectNodes::While &node)
			{
				if (node.DoWhile)
				{
					this->mCurrentSource += "do\n{\n";

					if (node.Statements != 0)
					{
						this->mAST[node.Statements].Accept(*this);
					}

					this->mCurrentSource += "}\n";
					this->mCurrentSource += "while (";
					this->mAST[node.Condition].Accept(*this);
					this->mCurrentSource += ");\n";
				}
				else
				{
					this->mCurrentSource += "while (";
					this->mAST[node.Condition].Accept(*this);
					this->mCurrentSource += ")\n";

					if (node.Statements != 0)
					{
						this->mAST[node.Statements].Accept(*this);
					}
					else
					{
						this->mCurrentSource += "\t;";
					}
				}
			}
			void												Visit(const EffectNodes::Jump &node)
			{
				switch (node.Mode)
				{
					case EffectNodes::Jump::Return:
						this->mCurrentSource += "return";

						if (node.Value != 0)
						{
							this->mCurrentSource += ' ';
							this->mAST[node.Value].Accept(*this);
						}
						break;
					case EffectNodes::Jump::Break:
						this->mCurrentSource += "break";
						break;
					case EffectNodes::Jump::Continue:
						this->mCurrentSource += "continue";
						break;
					case EffectNodes::Jump::Discard:
						this->mCurrentSource += "discard";
						break;
				}

				this->mCurrentSource += ";\n";
			}
			void												Visit(const EffectNodes::ExpressionStatement &node)
			{
				if (node.Expression != 0)
				{
					this->mAST[node.Expression].Accept(*this);
				}

				this->mCurrentSource += ";\n";
			}
			void												Visit(const EffectNodes::StatementBlock &node)
			{
				this->mCurrentSource += "{\n";

				for (unsigned int i = 0; i < node.Length; ++i)
				{
					this->mAST[node[i]].Accept(*this);
				}

				this->mCurrentSource += "}\n";
			}
			void												Visit(const EffectNodes::Annotation &node)
			{
				Effect::Annotation annotation;
				const auto &value = this->mAST[node.Value].As<EffectNodes::Literal>();

				switch (value.Type.Class)
				{
					case EffectNodes::Type::Bool:
						annotation = value.Value.Bool[0] != 0;
						break;
					case EffectNodes::Type::Int:
						annotation = value.Value.Int[0];
						break;
					case EffectNodes::Type::Uint:
						annotation = value.Value.Uint[0];
						break;
					case EffectNodes::Type::Float:
						annotation = value.Value.Float[0];
						break;
					case EffectNodes::Type::String:
						annotation = value.Value.String;
						break;
				}

				assert(this->mCurrentAnnotations != nullptr);

				this->mCurrentAnnotations->insert(std::make_pair(node.Name, annotation));
			}
			void												Visit(const EffectNodes::Struct &node)
			{
				this->mCurrentSource += "struct ";

				if (node.Name != nullptr)
				{
					this->mCurrentSource += FixName(node.Name);
				}
				else
				{
					this->mCurrentSource += "_" + std::to_string(std::rand() * 100 + std::rand() * 10 + std::rand());
				}

				this->mCurrentSource += "\n{\n";

				if (node.HasFields())
				{
					const auto &fields = this->mAST[node.Fields].As<EffectNodes::List>();

					for (unsigned int i = 0; i < fields.Length; ++i)
					{
						this->mAST[fields[i]].As<EffectNodes::Variable>().Accept(*this);
					}
				}
				else
				{
					this->mCurrentSource += "float _dummy;\n";
				}

				this->mCurrentSource += "};\n";
			}
			void												Visit(const EffectNodes::Variable &node)
			{
				if (!(this->mCurrentInParameterBlock || this->mCurrentInFunctionBlock))
				{
					if (node.Type.IsStruct() && node.Type.HasQualifier(EffectNodes::Type::Qualifier::Uniform))
					{
						VisitUniformBuffer(node);
						return;
					}
					else if (node.Type.IsTexture())
					{
						VisitTexture(node);
						return;
					}
					else if (node.Type.IsSampler())
					{
						VisitSampler(node);
						return;
					}
					else if (node.Type.HasQualifier(EffectNodes::Type::Qualifier::Uniform))
					{
						VisitUniform(node);
						return;
					}
				}

				this->mCurrentSource += PrintTypeWithQualifiers(node.Type);

				if (node.Name != nullptr)
				{
					this->mCurrentSource += ' ';

					if (!this->mCurrentBlockName.empty())
					{
						this->mCurrentSource += this->mCurrentBlockName + '_';
					}
				
					this->mCurrentSource += FixName(node.Name);
				}

				if (node.Type.IsArray())
				{
					this->mCurrentSource += '[';
					this->mCurrentSource += (node.Type.ArrayLength >= 1) ? std::to_string(node.Type.ArrayLength) : "";
					this->mCurrentSource += ']';
				}

				if (node.HasInitializer())
				{
					this->mCurrentSource += " = ";

					const auto cast = PrintCast(this->mAST[node.Initializer].As<EffectNodes::RValue>().Type, node.Type);

					this->mCurrentSource += cast.first;
					this->mAST[node.Initializer].Accept(*this);
					this->mCurrentSource += cast.second;
				}

				if (!this->mCurrentInParameterBlock)
				{
					this->mCurrentSource += ";\n";
				}
			}
			void												VisitTexture(const EffectNodes::Variable &node)
			{			
				const GLsizei width = (node.Properties[EffectNodes::Variable::Width] != 0) ? this->mAST[node.Properties[EffectNodes::Variable::Width]].As<EffectNodes::Literal>().Value.Uint[0] : 1;
				const GLsizei height = (node.Properties[EffectNodes::Variable::Height] != 0) ? this->mAST[node.Properties[EffectNodes::Variable::Height]].As<EffectNodes::Literal>().Value.Uint[0] : 1;
				const GLsizei levels = (node.Properties[EffectNodes::Variable::MipLevels] != 0) ? this->mAST[node.Properties[EffectNodes::Variable::MipLevels]].As<EffectNodes::Literal>().Value.Uint[0] : 1;
				GLenum internalformat = GL_RGBA8, internalformatSRGB = GL_SRGB8_ALPHA8;
				Effect::Texture::Format format = Effect::Texture::Format::RGBA8;

				if (node.Properties[EffectNodes::Variable::Format] != 0)
				{
					LiteralToFormat(this->mAST[node.Properties[EffectNodes::Variable::Format]].As<EffectNodes::Literal>().Value.Uint[0], internalformat, internalformatSRGB, format);
				}

				GLuint textures[2] = { 0, 0 };

				GLCHECK(glGenTextures(2, textures));

				std::unique_ptr<OGL4Texture> obj(new OGL4Texture(this->mEffect));
				obj->mDesc.Width = width;
				obj->mDesc.Height = height;
				obj->mDesc.Levels = levels;
				obj->mDesc.Format = format;
				obj->mID = textures[0];
				obj->mSRGBView = textures[1];

				GLint previous = 0;
				GLCHECK(glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous));

				GLCHECK(glBindTexture(GL_TEXTURE_2D, textures[0]));
				GLCHECK(glTexStorage2D(GL_TEXTURE_2D, levels, internalformat, width, height));
				const std::vector<unsigned char> nullpixels(width * height, 0);
				GLCHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, nullpixels.data()));
				GLCHECK(glTextureView(textures[1], GL_TEXTURE_2D, textures[0], internalformatSRGB, 0, levels, 0, 1));

				GLCHECK(glBindTexture(GL_TEXTURE_2D, previous));

				if (node.HasAnnotations())
				{
					const auto &annotations = this->mAST[node.Annotations].As<EffectNodes::List>();

					this->mCurrentAnnotations = &obj->mAnnotations;

					for (unsigned int i = 0; i < annotations.Length; ++i)
					{
						this->mAST[annotations[i]].Accept(*this);
					}

					this->mCurrentAnnotations = nullptr;
				}

				this->mEffect->mTextures.insert(std::make_pair(node.Name, std::move(obj)));
			}
			void												VisitSampler(const EffectNodes::Variable &node)
			{
				if (node.Properties[EffectNodes::Variable::Texture] == 0)
				{
					this->mErrors = PrintLocation(node.Location) + "Sampler '" + std::string(node.Name) + "' is missing required 'Texture' required.\n";
					this->mFatal;
					return;
				}

				std::shared_ptr<OGL4Sampler> obj = std::make_shared<OGL4Sampler>();
				obj->mTexture = this->mEffect->mTextures.at(this->mAST[node.Properties[EffectNodes::Variable::Texture]].As<EffectNodes::Variable>().Name).get();
				obj->mSRGB = node.Properties[EffectNodes::Variable::SRGBTexture] != 0 && this->mAST[node.Properties[EffectNodes::Variable::SRGBTexture]].As<EffectNodes::Literal>().Value.Bool[0] != 0;

				GLCHECK(glGenSamplers(1, &obj->mID));

				GLenum minfilter = (node.Properties[EffectNodes::Variable::MinFilter] != 0) ? LiteralToTextureFilter(this->mAST[node.Properties[EffectNodes::Variable::MinFilter]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_LINEAR;
				const GLenum mipfilter = (node.Properties[EffectNodes::Variable::MipFilter] != 0) ? LiteralToTextureFilter(this->mAST[node.Properties[EffectNodes::Variable::MipFilter]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_LINEAR;
				const GLenum mipfilters[2][2] =
				{
					{ GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST },
					{ GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR }
				};

				minfilter = mipfilters[minfilter - GL_NEAREST][mipfilter - GL_NEAREST];

				GLCHECK(glSamplerParameteri(obj->mID, GL_TEXTURE_WRAP_S, (node.Properties[EffectNodes::Variable::AddressU] != 0) ? LiteralToTextureWrap(this->mAST[node.Properties[EffectNodes::Variable::AddressU]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_CLAMP_TO_EDGE));
				GLCHECK(glSamplerParameteri(obj->mID, GL_TEXTURE_WRAP_T, (node.Properties[EffectNodes::Variable::AddressV] != 0) ? LiteralToTextureWrap(this->mAST[node.Properties[EffectNodes::Variable::AddressV]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_CLAMP_TO_EDGE));
				GLCHECK(glSamplerParameteri(obj->mID, GL_TEXTURE_WRAP_R, (node.Properties[EffectNodes::Variable::AddressW] != 0) ? LiteralToTextureWrap(this->mAST[node.Properties[EffectNodes::Variable::AddressW]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_CLAMP_TO_EDGE));
				GLCHECK(glSamplerParameteri(obj->mID, GL_TEXTURE_MIN_FILTER, minfilter));
				GLCHECK(glSamplerParameteri(obj->mID, GL_TEXTURE_MAG_FILTER, (node.Properties[EffectNodes::Variable::MagFilter] != 0) ? LiteralToTextureFilter(this->mAST[node.Properties[EffectNodes::Variable::MagFilter]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_LINEAR));
				GLCHECK(glSamplerParameterf(obj->mID, GL_TEXTURE_LOD_BIAS, (node.Properties[EffectNodes::Variable::MipLODBias] != 0) ? this->mAST[node.Properties[EffectNodes::Variable::MipLODBias]].As<EffectNodes::Literal>().Value.Float[0] : 0.0f));
				GLCHECK(glSamplerParameterf(obj->mID, GL_TEXTURE_MIN_LOD, (node.Properties[EffectNodes::Variable::MinLOD] != 0) ? this->mAST[node.Properties[EffectNodes::Variable::MinLOD]].As<EffectNodes::Literal>().Value.Float[0] : -1000.0f));
				GLCHECK(glSamplerParameterf(obj->mID, GL_TEXTURE_MAX_LOD, (node.Properties[EffectNodes::Variable::MaxLOD] != 0) ? this->mAST[node.Properties[EffectNodes::Variable::MaxLOD]].As<EffectNodes::Literal>().Value.Float[0] : 1000.0f));
	#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
				GLCHECK(glSamplerParameterf(obj->mID, GL_TEXTURE_MAX_ANISOTROPY_EXT, (node.Properties[EffectNodes::Variable::MaxAnisotropy] != 0) ? static_cast<GLfloat>(this->mAST[node.Properties[EffectNodes::Variable::MaxAnisotropy]].As<EffectNodes::Literal>().Value.Uint[0]) : 1.0f));

				this->mCurrentSource += "layout(binding = " + std::to_string(this->mEffect->mSamplers.size()) + ") uniform sampler2D ";
				this->mCurrentSource += FixName(node.Name);
				this->mCurrentSource += ";\n";

				this->mEffect->mSamplers.push_back(obj);
			}
			void												VisitUniform(const EffectNodes::Variable &node)
			{
				this->mCurrentGlobalConstants += PrintTypeWithQualifiers(node.Type);
				this->mCurrentGlobalConstants += ' ';

				if (!this->mCurrentBlockName.empty())
				{
					this->mCurrentGlobalConstants += this->mCurrentBlockName + '_';
				}
				
				this->mCurrentGlobalConstants += node.Name;

				if (node.Type.IsArray())
				{
					this->mCurrentGlobalConstants += '[';
					this->mCurrentGlobalConstants += (node.Type.ArrayLength >= 1) ? std::to_string(node.Type.ArrayLength) : "";
					this->mCurrentGlobalConstants += ']';
				}

				this->mCurrentGlobalConstants += ";\n";

				std::unique_ptr<OGL4Constant> obj(new OGL4Constant(this->mEffect));
				obj->mDesc.Rows = node.Type.Rows;
				obj->mDesc.Columns = node.Type.Cols;
				obj->mDesc.Elements = node.Type.ArrayLength;
				obj->mDesc.Fields = 0;
				obj->mDesc.Size = node.Type.Rows * node.Type.Cols;

				switch (node.Type.Class)
				{
					case EffectNodes::Type::Bool:
						obj->mDesc.Size *= sizeof(int);
						obj->mDesc.Type = Effect::Constant::Type::Bool;
						break;
					case EffectNodes::Type::Int:
						obj->mDesc.Size *= sizeof(int);
						obj->mDesc.Type = Effect::Constant::Type::Int;
						break;
					case EffectNodes::Type::Uint:
						obj->mDesc.Size *= sizeof(unsigned int);
						obj->mDesc.Type = Effect::Constant::Type::Uint;
						break;
					case EffectNodes::Type::Float:
						obj->mDesc.Size *= sizeof(float);
						obj->mDesc.Type = Effect::Constant::Type::Float;
						break;
				}

				const std::size_t alignment = 16 - (this->mCurrentGlobalSize % 16);
				this->mCurrentGlobalSize += (obj->mDesc.Size > alignment && (alignment != 16 || obj->mDesc.Size <= 16)) ? obj->mDesc.Size + alignment : obj->mDesc.Size;

				obj->mBuffer = 0;
				obj->mBufferOffset = this->mCurrentGlobalSize - obj->mDesc.Size;

				if (this->mCurrentGlobalSize >= this->mEffect->mUniformStorages[0].second)
				{
					this->mEffect->mUniformStorages[0].first = static_cast<unsigned char *>(::realloc(this->mEffect->mUniformStorages[0].first, this->mEffect->mUniformStorages[0].second += 128));
				}

				if (node.HasInitializer())
				{
					std::memcpy(this->mEffect->mUniformStorages[0].first + obj->mBufferOffset, &this->mAST[node.Initializer].As<EffectNodes::Literal>().Value, obj->mDesc.Size);
				}
				else
				{
					std::memset(this->mEffect->mUniformStorages[0].first + obj->mBufferOffset, 0, obj->mDesc.Size);
				}

				this->mEffect->mConstants.insert(std::make_pair(node.Name, std::move(obj)));
			}
			void												VisitUniformBuffer(const EffectNodes::Variable &node)
			{
				const auto &structure = this->mAST[node.Type.Definition].As<EffectNodes::Struct>();

				if (!structure.HasFields())
				{
					return;
				}

				this->mCurrentSource += "layout(std140, binding = " + std::to_string(this->mEffect->mUniformBuffers.size()) + ") uniform ";
				this->mCurrentSource += FixName(node.Name);
				this->mCurrentSource += "\n{\n";

				this->mCurrentBlockName = node.Name;

				unsigned char *storage = nullptr;
				std::size_t totalsize = 0, currentsize = 0;

				const auto &fields = this->mAST[structure.Fields].As<EffectNodes::List>();

				for (unsigned int i = 0; i < fields.Length; ++i)
				{
					const auto &field = this->mAST[fields[i]].As<EffectNodes::Variable>();

					field.Accept(*this);

					std::unique_ptr<OGL4Constant> obj(new OGL4Constant(this->mEffect));
					obj->mDesc.Rows = field.Type.Rows;
					obj->mDesc.Columns = field.Type.Cols;
					obj->mDesc.Elements = field.Type.ArrayLength;
					obj->mDesc.Fields = 0;
					obj->mDesc.Size = field.Type.Rows * field.Type.Cols;

					switch (field.Type.Class)
					{
						case EffectNodes::Type::Bool:
							obj->mDesc.Size *= sizeof(int);
							obj->mDesc.Type = Effect::Constant::Type::Bool;
							break;
						case EffectNodes::Type::Int:
							obj->mDesc.Size *= sizeof(int);
							obj->mDesc.Type = Effect::Constant::Type::Int;
							break;
						case EffectNodes::Type::Uint:
							obj->mDesc.Size *= sizeof(unsigned int);
							obj->mDesc.Type = Effect::Constant::Type::Uint;
							break;
						case EffectNodes::Type::Float:
							obj->mDesc.Size *= sizeof(float);
							obj->mDesc.Type = Effect::Constant::Type::Float;
							break;
					}

					const std::size_t alignment = 16 - (totalsize % 16);
					totalsize += (obj->mDesc.Size > alignment && (alignment != 16 || obj->mDesc.Size <= 16)) ? obj->mDesc.Size + alignment : obj->mDesc.Size;

					obj->mBuffer = this->mEffect->mUniformBuffers.size();
					obj->mBufferOffset = totalsize - obj->mDesc.Size;

					if (totalsize >= currentsize)
					{
						storage = static_cast<unsigned char *>(::realloc(storage, currentsize += 128));
					}

					if (field.HasInitializer())
					{
						std::memcpy(storage + obj->mBufferOffset, &this->mAST[field.Initializer].As<EffectNodes::Literal>().Value, obj->mDesc.Size);
					}
					else
					{
						std::memset(storage + obj->mBufferOffset, 0, obj->mDesc.Size);
					}

					this->mEffect->mConstants.insert(std::make_pair(std::string(node.Name) + '.' + std::string(field.Name), std::move(obj)));
				}

				this->mCurrentBlockName.clear();

				this->mCurrentSource += "};\n";

				std::unique_ptr<OGL4Constant> obj(new OGL4Constant(this->mEffect));
				obj->mDesc.Type = Effect::Constant::Type::Struct;
				obj->mDesc.Rows = 0;
				obj->mDesc.Columns = 0;
				obj->mDesc.Elements = 0;
				obj->mDesc.Fields = fields.Length;
				obj->mDesc.Size = totalsize;
				obj->mBuffer = this->mEffect->mUniformBuffers.size();
				obj->mBufferOffset = 0;

				if (node.HasAnnotations())
				{
					const auto &annotations = this->mAST[node.Annotations].As<EffectNodes::List>();

					this->mCurrentAnnotations = &obj->mAnnotations;

					for (unsigned int i = 0; i < annotations.Length; ++i)
					{
						this->mAST[annotations[i]].As<EffectNodes::Annotation>().Accept(*this);
					}

					this->mCurrentAnnotations = nullptr;
				}

				this->mEffect->mConstants.insert(std::make_pair(node.Name, std::move(obj)));

				GLuint buffer = 0;
				glGenBuffers(1, &buffer);

				GLint previous = 0;
				glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &previous);

				glBindBuffer(GL_UNIFORM_BUFFER, buffer);
				glBufferData(GL_UNIFORM_BUFFER, totalsize, storage, GL_DYNAMIC_DRAW);

				glBindBuffer(GL_UNIFORM_BUFFER, previous);

				this->mEffect->mUniformBuffers.push_back(buffer);
				this->mEffect->mUniformStorages.push_back(std::make_pair(storage, totalsize));
			}
			void												Visit(const EffectNodes::Function &node)
			{
				this->mCurrentSource += PrintType(node.ReturnType);
				this->mCurrentSource += ' ';
				this->mCurrentSource += FixName(node.Name);
				this->mCurrentSource += '(';

				if (node.HasParameters())
				{
					const auto &parameters = this->mAST[node.Parameters].As<EffectNodes::List>();

					this->mCurrentInParameterBlock = true;

					for (unsigned int i = 0; i < parameters.Length; ++i)
					{
						this->mAST[parameters[i]].As<EffectNodes::Variable>().Accept(*this);

						this->mCurrentSource += ", ";
					}

					this->mCurrentSource.pop_back();
					this->mCurrentSource.pop_back();

					this->mCurrentInParameterBlock = false;
				}

				this->mCurrentSource += ')';

				if (node.HasDefinition())
				{
					this->mCurrentSource += '\n';
					this->mCurrentInFunctionBlock = true;

					this->mAST[node.Definition].As<EffectNodes::StatementBlock>().Accept(*this);

					this->mCurrentInFunctionBlock = false;
				}
				else
				{
					this->mCurrentSource += ";\n";
				}
			}
			void												Visit(const EffectNodes::Technique &node)
			{
				std::unique_ptr<OGL4Technique> obj(new OGL4Technique(this->mEffect));

				const auto &passes = this->mAST[node.Passes].As<EffectNodes::List>();

				obj->mDesc.Passes.reserve(passes.Length);
				obj->mPasses.reserve(passes.Length);

				this->mCurrentPasses = &obj->mPasses;

				for (unsigned int i = 0; i < passes.Length; ++i)
				{
					const auto &pass = this->mAST[passes[i]].As<EffectNodes::Pass>();

					obj->mDesc.Passes.push_back(pass.Name != nullptr ? pass.Name : "");
					
					pass.Accept(*this);
				}

				this->mCurrentPasses = nullptr;

				if (node.HasAnnotations())
				{
					const auto &annotations = this->mAST[node.Annotations].As<EffectNodes::List>();

					this->mCurrentAnnotations = &obj->mAnnotations;

					for (unsigned int i = 0; i < annotations.Length; ++i)
					{
						this->mAST[annotations[i]].As<EffectNodes::Annotation>().Accept(*this);
					}

					this->mCurrentAnnotations = nullptr;
				}

				this->mEffect->mTechniques.insert(std::make_pair(node.Name, std::move(obj)));
			}
			void												Visit(const EffectNodes::Pass &node)
			{
				OGL4Technique::Pass pass;
				ZeroMemory(&pass, sizeof(OGL4Technique::Pass));

				if (node.States[EffectNodes::Pass::ColorWriteMask] != 0)
				{
					const GLuint mask = this->mAST[node.States[EffectNodes::Pass::ColorWriteMask]].As<EffectNodes::Literal>().Value.Uint[0];

					pass.ColorMaskR = (mask & (1 << 0)) != 0;
					pass.ColorMaskG = (mask & (1 << 1)) != 0;
					pass.ColorMaskB = (mask & (1 << 2)) != 0;
					pass.ColorMaskA = (mask & (1 << 3)) != 0;
				}
				else
				{
					pass.ColorMaskR = pass.ColorMaskG = pass.ColorMaskB = pass.ColorMaskA = GL_TRUE;
				}

				pass.DepthTest = node.States[EffectNodes::Pass::DepthEnable] != 0 && this->mAST[node.States[EffectNodes::Pass::DepthEnable]].As<EffectNodes::Literal>().Value.Bool[0];
				pass.DepthMask = node.States[EffectNodes::Pass::DepthWriteMask] == 0 || this->mAST[node.States[EffectNodes::Pass::DepthWriteMask]].As<EffectNodes::Literal>().Value.Bool[0];
				pass.DepthFunc = (node.States[EffectNodes::Pass::DepthFunc] != 0) ? LiteralToCompFunc(this->mAST[node.States[EffectNodes::Pass::DepthFunc]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_LESS;
				pass.StencilTest = node.States[EffectNodes::Pass::StencilEnable] != 0 && this->mAST[node.States[EffectNodes::Pass::StencilEnable]].As<EffectNodes::Literal>().Value.Bool[0];
				pass.StencilReadMask = (node.States[EffectNodes::Pass::StencilReadMask] != 0) ? static_cast<UINT8>(this->mAST[node.States[EffectNodes::Pass::StencilReadMask]].As<EffectNodes::Literal>().Value.Uint[0] & 0xFF) : 0xFFFFFFFF;
				pass.StencilMask = (node.States[EffectNodes::Pass::StencilWriteMask] != 0) != 0 ? static_cast<UINT8>(this->mAST[node.States[EffectNodes::Pass::StencilWriteMask]].As<EffectNodes::Literal>().Value.Uint[0] & 0xFF) : 0xFFFFFFFF;
				pass.StencilFunc = (node.States[EffectNodes::Pass::StencilFunc] != 0) ? LiteralToCompFunc(this->mAST[node.States[EffectNodes::Pass::StencilFunc]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_ALWAYS;
				pass.StencilOpZPass = (node.States[EffectNodes::Pass::StencilPass] != 0) ? LiteralToStencilOp(this->mAST[node.States[EffectNodes::Pass::StencilPass]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_KEEP;
				pass.StencilOpFail = (node.States[EffectNodes::Pass::StencilFail] != 0) ? LiteralToStencilOp(this->mAST[node.States[EffectNodes::Pass::StencilFail]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_KEEP;
				pass.StencilOpZFail = (node.States[EffectNodes::Pass::StencilDepthFail] != 0) ? LiteralToStencilOp(this->mAST[node.States[EffectNodes::Pass::StencilDepthFail]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_KEEP;
				pass.Blend = node.States[EffectNodes::Pass::BlendEnable] != 0 && this->mAST[node.States[EffectNodes::Pass::BlendEnable]].As<EffectNodes::Literal>().Value.Bool[0];
				pass.BlendEqColor = (node.States[EffectNodes::Pass::BlendOp] != 0) ? LiteralToBlendEq(this->mAST[node.States[EffectNodes::Pass::BlendOp]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_FUNC_ADD;
				pass.BlendEqAlpha = (node.States[EffectNodes::Pass::BlendOpAlpha] != 0) ? LiteralToBlendEq(this->mAST[node.States[EffectNodes::Pass::BlendOpAlpha]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_FUNC_ADD;
				pass.BlendFuncSrc = (node.States[EffectNodes::Pass::SrcBlend] != 0) ? LiteralToBlendFunc(this->mAST[node.States[EffectNodes::Pass::SrcBlend]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_ONE;
				pass.BlendFuncDest = (node.States[EffectNodes::Pass::DestBlend] != 0) ? LiteralToBlendFunc(this->mAST[node.States[EffectNodes::Pass::DestBlend]].As<EffectNodes::Literal>().Value.Uint[0]) : GL_ZERO;

				if (node.States[EffectNodes::Pass::SRGBWriteEnable] != 0)
				{
					pass.FramebufferSRGB = this->mAST[node.States[EffectNodes::Pass::SRGBWriteEnable]].As<EffectNodes::Literal>().Value.Bool[0] != 0;
				}

				for (unsigned int i = 0; i < 8; ++i)
				{
					if (node.States[EffectNodes::Pass::RenderTarget0 + i] != 0)
					{
						const auto &variable = this->mAST[node.States[EffectNodes::Pass::RenderTarget0 + i]].As<EffectNodes::Variable>();

						const auto it = this->mEffect->mTextures.find(variable.Name);

						if (it == this->mEffect->mTextures.end())
						{
							this->mFatal = true;
							return;
						}

						const OGL4Texture *texture = it->second.get();

						if ((texture->mDesc.Width != static_cast<unsigned int>(pass.ViewportWidth) || texture->mDesc.Height != static_cast<unsigned int>(pass.ViewportHeight)) && !(pass.ViewportWidth == 0 && pass.ViewportHeight == 0))
						{
							this->mErrors += PrintLocation(node.Location) + "Cannot use multiple rendertargets with different sized textures.\n";
							this->mFatal = true;
							return;
						}

						pass.ViewportWidth = texture->mDesc.Width;
						pass.ViewportHeight = texture->mDesc.Height;

						if (pass.Framebuffer == 0)
						{
							glGenFramebuffers(1, &pass.Framebuffer);
							glBindFramebuffer(GL_FRAMEBUFFER, pass.Framebuffer);
						}

						glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, pass.FramebufferSRGB ? texture->mSRGBView : texture->mID, 0);

						pass.DrawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
					}
				}

				if (pass.Framebuffer != 0)
				{
#ifdef _DEBUG
					GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
					assert(status == GL_FRAMEBUFFER_COMPLETE);
#endif
					glBindFramebuffer(GL_FRAMEBUFFER, 0);
				}
				else
				{
					RECT rect;
					::GetClientRect(::WindowFromDC(wglGetCurrentDC()), &rect);

					pass.ViewportWidth = static_cast<GLsizei>(rect.right - rect.left);
					pass.ViewportHeight = static_cast<GLsizei>(rect.bottom - rect.top);
				}

				pass.Program = glCreateProgram();

				GLuint shaders[2] = { 0, 0 };

				if (node.States[EffectNodes::Pass::VertexShader] != 0)
				{
					shaders[0] = VisitShader(this->mAST[node.States[EffectNodes::Pass::VertexShader]].As<EffectNodes::Function>(), EffectNodes::Pass::VertexShader);
					glAttachShader(pass.Program, shaders[0]);
				}
				if (node.States[EffectNodes::Pass::PixelShader] != 0)
				{
					shaders[1] = VisitShader(this->mAST[node.States[EffectNodes::Pass::PixelShader]].As<EffectNodes::Function>(), EffectNodes::Pass::PixelShader);
					glAttachShader(pass.Program, shaders[1]);
				}

				glLinkProgram(pass.Program);

				for (unsigned int i = 0; i < 2; ++i)
				{
					if (shaders[i] == 0)
					{
						continue;
					}

					glDetachShader(pass.Program, shaders[i]);
					glDeleteShader(shaders[i]);
				}

				GLint status = GL_FALSE;

				glGetProgramiv(pass.Program, GL_LINK_STATUS, &status);

				if (status == GL_FALSE)
				{
					GLint logsize = 0;
					glGetProgramiv(pass.Program, GL_INFO_LOG_LENGTH, &logsize);

					std::string log(logsize, '\0');
					glGetProgramInfoLog(pass.Program, logsize, nullptr, &log.front());

					glDeleteProgram(pass.Program);

					this->mErrors += log;
					this->mFatal = true;
					return;
				}

				assert(this->mCurrentPasses != nullptr);

				this->mCurrentPasses->push_back(std::move(pass));
			}
			GLuint												VisitShader(const EffectNodes::Function &node, unsigned int type)
			{
				std::string source =
					"#version 430\n"
					"float _fmod(float x, float y) { return x - y * trunc(x / y); }"
					"vec2 _fmod(vec2 x, vec2 y) { return x - y * trunc(x / y); }"
					"vec3 _fmod(vec3 x, vec3 y) { return x - y * trunc(x / y); }"
					"vec4 _fmod(vec4 x, vec4 y) { return x - y * trunc(x / y); }"
					"mat2 _fmod(mat2 x, mat2 y) { return x - matrixCompMult(y, mat2(trunc(x[0] / y[0]), trunc(x[1] / y[1]))); }"
					"mat3 _fmod(mat3 x, mat3 y) { return x - matrixCompMult(y, mat3(trunc(x[0] / y[0]), trunc(x[1] / y[1]), trunc(x[2] / y[2]))); }"
					"mat4 _fmod(mat4 x, mat4 y) { return x - matrixCompMult(y, mat4(trunc(x[0] / y[0]), trunc(x[1] / y[1]), trunc(x[2] / y[2]), trunc(x[3] / y[3]))); }\n"
					"void _sincos(float x, out float s, out float c) { s = sin(x), c = cos(x); }"
					"void _sincos(vec2 x, out vec2 s, out vec2 c) { s = sin(x), c = cos(x); }"
					"void _sincos(vec3 x, out vec3 s, out vec3 c) { s = sin(x), c = cos(x); }"
					"void _sincos(vec4 x, out vec4 s, out vec4 c) { s = sin(x), c = cos(x); }\n"
					"vec4 _textureLod(sampler2D s, vec4 c) { return textureLod(s, c.xy, c.w); }\n"
					"#define _textureLodOffset(s, c, offset) textureLodOffset(s, (c).xy, (c).w, offset)\n"
					"vec4 _textureBias(sampler2D s, vec4 c) { return textureOffset(s, c.xy, ivec2(0), c.w); }\n";

				if (type != EffectNodes::Pass::PixelShader)
				{
					source += "#define discard\n";
				}

				source += this->mCurrentSource;

				if (node.HasParameters())
				{
					const auto &parameters = this->mAST[node.Parameters].As<EffectNodes::List>();

					for (unsigned int i = 0; i < parameters.Length; ++i)
					{
						const auto &parameter = this->mAST[parameters[i]].As<EffectNodes::Variable>();

						if (parameter.Type.IsStruct())
						{
							if (this->mAST[parameter.Type.Definition].As<EffectNodes::Struct>().HasFields())
							{
								const auto &fields = this->mAST[this->mAST[parameter.Type.Definition].As<EffectNodes::Struct>().Fields].As<EffectNodes::List>();

								for (unsigned int k = 0; k < fields.Length; ++k)
								{
									const auto &field = this->mAST[fields[i]].As<EffectNodes::Variable>();

									VisitShaderVariable(parameter.Type.Qualifiers, field.Type, "_param_" + std::string(parameter.Name) + "_" + std::string(field.Name), parameter.Semantic, source);
								}
							}
						}
						else
						{
							VisitShaderVariable(parameter.Type.Qualifiers, parameter.Type, "_param_" + std::string(parameter.Name), parameter.Semantic, source);
						}
					}
				}

				if (node.ReturnType.IsStruct())
				{
					if (this->mAST[node.ReturnType.Definition].As<EffectNodes::Struct>().HasFields())
					{
						const auto &fields = this->mAST[this->mAST[node.ReturnType.Definition].As<EffectNodes::Struct>().Fields].As<EffectNodes::List>();

						for (unsigned int i = 0; i < fields.Length; ++i)
						{
							const auto &field = this->mAST[fields[i]].As<EffectNodes::Variable>();

							VisitShaderVariable(EffectNodes::Type::Out, field.Type, "_return_" + std::string(field.Name), field.Semantic, source);
						}
					}
				}
				else if (!node.ReturnType.IsVoid())
				{
					VisitShaderVariable(EffectNodes::Type::Out, node.ReturnType, "_return", node.ReturnSemantic, source);
				}

				source += "void main()\n{\n";

				if (node.HasParameters())
				{
					const auto &parameters = this->mAST[node.Parameters].As<EffectNodes::List>();
				
					for (unsigned int i = 0; i < parameters.Length; ++i)
					{
						const auto &parameter = this->mAST[parameters[i]].As<EffectNodes::Variable>();

						if (parameter.Type.IsStruct())
						{
							source += PrintType(parameter.Type) + " _param_" + std::string(parameter.Name) + " = " + this->mAST[parameter.Type.Definition].As<EffectNodes::Struct>().Name + "(";

							if (this->mAST[parameter.Type.Definition].As<EffectNodes::Struct>().HasFields())
							{
								const auto &fields = this->mAST[this->mAST[parameter.Type.Definition].As<EffectNodes::Struct>().Fields].As<EffectNodes::List>();

								for (unsigned int k = 0; k < fields.Length; ++k)
								{
									const auto &field = this->mAST[fields[i]].As<EffectNodes::Variable>();

									source += "_param_" + std::string(parameter.Name) + "_" + std::string(field.Name) + ", ";
								}

								source.pop_back();
								source.pop_back();
							}

							source += ")\n;";
						}
					}
				}
				if (node.ReturnType.IsStruct())
				{
					source += PrintType(node.ReturnType);
					source += " ";
				}

				if (!node.ReturnType.IsVoid())
				{
					source += "_return = ";
				}

				source += FixName(node.Name);
				source += "(";

				if (node.HasParameters())
				{
					const auto &parameters = this->mAST[node.Parameters].As<EffectNodes::List>();
				
					for (unsigned int i = 0; i < parameters.Length; ++i)
					{
						const auto &parameter = this->mAST[parameters[i]].As<EffectNodes::Variable>();

						if (::strcmp(parameter.Semantic, "SV_VERTEXID") == 0)
						{
							source += "gl_VertexID, ";
						}
						else if (::strcmp(parameter.Semantic, "SV_INSTANCEID") == 0)
						{
							source += "gl_InstanceID, ";
						}
						else if (::strcmp(parameter.Semantic, "SV_POSITION") == 0 && type == EffectNodes::Pass::VertexShader)
						{
							source += "gl_Position, ";
						}
						else if (::strcmp(parameter.Semantic, "SV_POSITION") == 0 && type == EffectNodes::Pass::PixelShader)
						{
							source += "gl_FragCoord, ";
						}
						else if (::strcmp(parameter.Semantic, "SV_DEPTH") == 0 && type == EffectNodes::Pass::PixelShader)
						{
							source += "gl_FragDepth, ";
						}
						else
						{
							source += "_param_" + std::string(parameter.Name) + ", ";
						}
					}

					source.pop_back();
					source.pop_back();
				}

				source += ");\n";

				if (node.HasParameters())
				{
					const auto &parameters = this->mAST[node.Parameters].As<EffectNodes::List>();
				
					for (unsigned int i = 0; i < parameters.Length; ++i)
					{
						const auto &parameter = this->mAST[parameters[i]].As<EffectNodes::Variable>();

						if (parameter.Type.IsStruct())
						{
							if (this->mAST[parameter.Type.Definition].As<EffectNodes::Struct>().HasFields())
							{
								const auto &fields = this->mAST[this->mAST[parameter.Type.Definition].As<EffectNodes::Struct>().Fields].As<EffectNodes::List>();

								for (unsigned int k = 0; k < fields.Length; ++k)
								{
									const auto &field = this->mAST[fields[i]].As<EffectNodes::Variable>();

									source += "_param_" + std::string(parameter.Name) + "_" + std::string(field.Name) + " = " + "_param_" + std::string(parameter.Name) + ";\n";
								}
							}
						}
					}
				}
				if (node.ReturnType.IsStruct() && this->mAST[node.ReturnType.Definition].As<EffectNodes::Struct>().HasFields())
				{
					const auto &fields = this->mAST[this->mAST[node.ReturnType.Definition].As<EffectNodes::Struct>().Fields].As<EffectNodes::List>();

					for (unsigned int i = 0; i < fields.Length; ++i)
					{
						const auto &field = this->mAST[fields[i]].As<EffectNodes::Variable>();

						source += "_return_" + std::string(field.Name) + " = _return." + std::string(field.Name) + ";\n";
					}
				}
			
				if (type == EffectNodes::Pass::VertexShader)
				{
					source += "gl_Position = gl_Position * vec4(1.0, -1.0, 2.0, 1.0) - vec4(0.0, 0.0, gl_Position.w, 0.0);\n";
				}
				/*if (type == EffectNodes::Pass::PixelShader)
				{
					source += "gl_FragDepth = clamp(gl_FragDepth, 0.0, 1.0);\n";
				}*/

				source += "}\n";

				GLuint shader;

				switch (type)
				{
					default:
						return 0;
					case EffectNodes::Pass::VertexShader:
						shader = glCreateShader(GL_VERTEX_SHADER);
						break;
					case EffectNodes::Pass::PixelShader:
						shader = glCreateShader(GL_FRAGMENT_SHADER);
						break;
				}

				const GLchar *src = source.c_str();
				const GLsizei len = static_cast<GLsizei>(source.length());

				LOG(TRACE) << "> Compiling shader '" << node.Name << "':\n\n" << source.c_str() << "\n";

				glShaderSource(shader, 1, &src, &len);
				glCompileShader(shader);

				GLint status = GL_FALSE;

				glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

				if (status == GL_FALSE)
				{
					GLint logsize = 0;
					glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logsize);

					std::string log(logsize, '\0');
					glGetShaderInfoLog(shader, logsize, nullptr, &log.front());

					glDeleteShader(shader);

					this->mErrors += log;
					this->mFatal = true;
					return 0;
				}

				return shader;
			}
			void												VisitShaderVariable(unsigned int qualifier, EffectNodes::Type type, const std::string &name, const char *semantic, std::string &source)
			{
				unsigned int location = 0;

				if (semantic == nullptr)
				{
					return;
				}
				else if (::strcmp(semantic, "SV_VERTEXID") == 0 || ::strcmp(semantic, "SV_INSTANCEID") == 0 || ::strcmp(semantic, "SV_POSITION") == 0)
				{
					return;
				}
				else if (::strstr(semantic, "SV_TARGET") == semantic)
				{
					location = static_cast<unsigned int>(::strtol(semantic + 9, nullptr, 10));
				}
				else if (::strstr(semantic, "TEXCOORD") == semantic)
				{
					location = static_cast<unsigned int>(::strtol(semantic + 8, nullptr, 10)) + 1;
				}

				source += "layout(location = " + std::to_string(location) + ") ";

				type.Qualifiers = static_cast<unsigned int>(qualifier);

				source += PrintTypeWithQualifiers(type) + ' ' + name;

				if (type.IsArray())
				{
					source += "[" + (type.ArrayLength >= 1 ? std::to_string(type.ArrayLength) : "") + "]";
				}

				source += ";\n";
			}

		private:
			const EffectTree &									mAST;
			OGL4Effect *										mEffect;
			std::string											mCurrentSource;
			std::string											mErrors;
			bool												mFatal;
			std::string											mCurrentGlobalConstants;
			std::size_t											mCurrentGlobalSize, mCurrentGlobalStorageSize;
			std::string											mCurrentBlockName;
			bool												mCurrentInParameterBlock, mCurrentInFunctionBlock;
			std::unordered_map<std::string, Effect::Annotation> *mCurrentAnnotations;
			std::vector<OGL4Technique::Pass> *					mCurrentPasses;
		};

		// -----------------------------------------------------------------------------------------------------

		OGL4EffectContext::OGL4EffectContext(HDC device, HGLRC context) : mDeviceContext(device), mRenderContext(context)
		{
		}
		OGL4EffectContext::~OGL4EffectContext(void)
		{
		}

		std::unique_ptr<Effect>									OGL4EffectContext::CreateEffect(const EffectTree &ast, std::string &errors) const
		{
			OGL4Effect *effect = new OGL4Effect(shared_from_this());
			
			OGL4EffectCompiler visitor(ast);
		
			if (visitor.Traverse(effect, errors))
			{
				return std::unique_ptr<Effect>(effect);
			}
			else
			{
				delete effect;

				return nullptr;
			}
		}
		void													OGL4EffectContext::CreateScreenshot(unsigned char *buffer, std::size_t size) const
		{
			GLCHECK(glReadBuffer(GL_BACK));

			const unsigned int pitch = this->mWidth * 4;

			assert(size >= pitch * this->mHeight);

			glReadPixels(0, 0, static_cast<GLsizei>(this->mWidth), static_cast<GLsizei>(this->mHeight), GL_RGBA, GL_UNSIGNED_BYTE, buffer);
			
			for (unsigned int y = 0; y * 2 < this->mHeight; ++y)
			{
				const unsigned int i1 = y * pitch;
				const unsigned int i2 = (this->mHeight - 1 - y) * pitch;

				for (unsigned int x = 0; x < pitch; x += 4)
				{
					buffer[i1 + x + 3] = 0xFF;
					buffer[i2 + x + 3] = 0xFF;

					std::swap(buffer[i1 + x + 0], buffer[i2 + x + 0]);
					std::swap(buffer[i1 + x + 1], buffer[i2 + x + 1]);
					std::swap(buffer[i1 + x + 2], buffer[i2 + x + 2]);
				}
			}
		}

		OGL4Effect::OGL4Effect(std::shared_ptr<const OGL4EffectContext> context) : mEffectContext(context), mDefaultVAO(0), mDefaultVBO(0), mDefaultFBO(0), mUniformDirty(true)
		{
			GLCHECK(glGenVertexArrays(1, &this->mDefaultVAO));
			GLCHECK(glGenBuffers(1, &this->mDefaultVBO));
			GLCHECK(glGenFramebuffers(1, &this->mDefaultFBO));

			GLint prevBuffer = 0;
			GLCHECK(glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuffer));
			GLCHECK(glBindBuffer(GL_ARRAY_BUFFER, this->mDefaultVBO));
			GLCHECK(glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW));
			GLCHECK(glBindBuffer(GL_ARRAY_BUFFER, prevBuffer));
		}
		OGL4Effect::~OGL4Effect(void)
		{
			GLCHECK(glDeleteVertexArrays(1, &this->mDefaultVAO));
			GLCHECK(glDeleteBuffers(1, &this->mDefaultVBO));
			GLCHECK(glDeleteFramebuffers(1, &this->mDefaultFBO));			
			GLCHECK(glDeleteBuffers(this->mUniformBuffers.size(), &this->mUniformBuffers.front()));
		}

		const Effect::Texture *									OGL4Effect::GetTexture(const std::string &name) const
		{
			auto it = this->mTextures.find(name);

			if (it == this->mTextures.end())
			{
				return nullptr;
			}

			return it->second.get();
		}
		std::vector<std::string>								OGL4Effect::GetTextureNames(void) const
		{
			std::vector<std::string> names;
			names.reserve(this->mTextures.size());

			for (auto it = this->mTextures.begin(), end = this->mTextures.end(); it != end; ++it)
			{
				names.push_back(it->first);
			}

			return names;
		}
		const Effect::Constant *								OGL4Effect::GetConstant(const std::string &name) const
		{
			auto it = this->mConstants.find(name);

			if (it == this->mConstants.end())
			{
				return nullptr;
			}

			return it->second.get();
		}
		std::vector<std::string>								OGL4Effect::GetConstantNames(void) const
		{
			std::vector<std::string> names;
			names.reserve(this->mConstants.size());

			for (auto it = this->mConstants.begin(), end = this->mConstants.end(); it != end; ++it)
			{
				names.push_back(it->first);
			}

			return names;
		}
		const Effect::Technique *								OGL4Effect::GetTechnique(const std::string &name) const
		{
			auto it = this->mTechniques.find(name);

			if (it == this->mTechniques.end())
			{
				return nullptr;
			}

			return it->second.get();
		}
		std::vector<std::string>								OGL4Effect::GetTechniqueNames(void) const
		{
			std::vector<std::string> names;
			names.reserve(this->mTechniques.size());

			for (auto it = this->mTechniques.begin(), end = this->mTechniques.end(); it != end; ++it)
			{
				names.push_back(it->first);
			}

			return names;
		}

		OGL4Texture::OGL4Texture(OGL4Effect *effect) : mEffect(effect), mID(0)
		{
		}
		OGL4Texture::~OGL4Texture(void)
		{
			GLCHECK(glDeleteTextures(1, &this->mID));
			GLCHECK(glDeleteTextures(1, &this->mSRGBView));
		}

		const Effect::Texture::Description						OGL4Texture::GetDescription(void) const
		{
			return this->mDesc;
		}
		const Effect::Annotation								OGL4Texture::GetAnnotation(const std::string &name) const
		{
			auto it = this->mAnnotations.find(name);

			if (it == this->mAnnotations.end())
			{
				return Effect::Annotation();
			}

			return it->second;
		}

		inline void												FlipBC1Block(unsigned char *block)
		{
			// BC1 Block:
			//  [0-1]  color 0
			//  [2-3]  color 1
			//  [4-7]  color indices

			std::swap(block[4], block[7]);
			std::swap(block[5], block[6]);
		}
		inline void												FlipBC2Block(unsigned char *block)
		{
			// BC2 Block:
			//  [0-7]  alpha indices
			//  [8-15] color block

			std::swap(block[0], block[6]);
			std::swap(block[1], block[7]);
			std::swap(block[2], block[4]);
			std::swap(block[3], block[5]);

			FlipBC1Block(block + 8);
		}
		inline void												FlipBC4Block(unsigned char *block)
		{
			// BC4 Block:
			//  [0]    red 0
			//  [1]    red 1
			//  [2-7]  red indices

			const unsigned int line_0_1 = block[2] + 256 * (block[3] + 256 * block[4]);
			const unsigned int line_2_3 = block[5] + 256 * (block[6] + 256 * block[7]);
			const unsigned int line_1_0 = ((line_0_1 & 0x000FFF) << 12) | ((line_0_1 & 0xFFF000) >> 12);
			const unsigned int line_3_2 = ((line_2_3 & 0x000FFF) << 12) | ((line_2_3 & 0xFFF000) >> 12);
			block[2] = static_cast<unsigned char>((line_3_2 & 0xFF));
			block[3] = static_cast<unsigned char>((line_3_2 & 0xFF00) >> 8);
			block[4] = static_cast<unsigned char>((line_3_2 & 0xFF0000) >> 16);
			block[5] = static_cast<unsigned char>((line_1_0 & 0xFF));
			block[6] = static_cast<unsigned char>((line_1_0 & 0xFF00) >> 8);
			block[7] = static_cast<unsigned char>((line_1_0 & 0xFF0000) >> 16);
		}
		inline void												FlipBC3Block(unsigned char *block)
		{
			// BC3 Block:
			//  [0-7]  alpha block
			//  [8-15] color block

			FlipBC4Block(block);
			FlipBC1Block(block + 8);
		}
		inline void												FlipBC5Block(unsigned char *block)
		{
			// BC5 Block:
			//  [0-7]  red block
			//  [8-15] green block

			FlipBC4Block(block);
			FlipBC4Block(block + 8);
		}
		void													FlipImage(const Effect::Texture::Description &desc, unsigned char *data)
		{
			typedef void (*FlipBlockFunc)(unsigned char *block);

			std::size_t blocksize = 0;
			bool compressed = false;
			FlipBlockFunc compressedFunc = nullptr;

			switch (desc.Format)
			{
				case Effect::Texture::Format::R8:
					blocksize = 1;
					break;
				case Effect::Texture::Format::RG8:
					blocksize = 2;
					break;
				case Effect::Texture::Format::R32F:
				case Effect::Texture::Format::RGBA8:
					blocksize = 4;
					break;
				case Effect::Texture::Format::RGBA16:
				case Effect::Texture::Format::RGBA16F:
					blocksize = 8;
					break;
				case Effect::Texture::Format::RGBA32F:
					blocksize = 16;
					break;
				case Effect::Texture::Format::DXT1:
					blocksize = 8;
					compressed = true;
					compressedFunc = &FlipBC1Block;
					break;
				case Effect::Texture::Format::DXT3:
					blocksize = 16;
					compressed = true;
					compressedFunc = &FlipBC2Block;
					break;
				case Effect::Texture::Format::DXT5:
					blocksize = 16;
					compressed = true;
					compressedFunc = &FlipBC3Block;
					break;
				case Effect::Texture::Format::LATC1:
					blocksize = 8;
					compressed = true;
					compressedFunc = &FlipBC4Block;
					break;
				case Effect::Texture::Format::LATC2:
					blocksize = 16;
					compressed = true;
					compressedFunc = &FlipBC5Block;
					break;
				default:
					return;
			}

			if (compressed)
			{
				const std::size_t w = (desc.Width + 3) / 4;
				const std::size_t h = (desc.Height + 3) / 4;
				const std::size_t stride = w * blocksize;

				for (std::size_t y = 0; y < h; ++y)
				{
					unsigned char *dataLine = data + stride * (h - 1 - y);

					for (std::size_t x = 0; x < stride; x += blocksize)
					{
						compressedFunc(dataLine + x);
					}
				}
			}
			else
			{
				const std::size_t w = desc.Width;
				const std::size_t h = desc.Height;
				const std::size_t stride = w * blocksize;
				unsigned char *templine = static_cast<unsigned char *>(::alloca(stride));

				for (std::size_t y = 0; 2 * y < h; ++y)
				{
					unsigned char *line1 = data + stride * y;
					unsigned char *line2 = data + stride * (h - 1 - y);

					::memcpy(templine, line1, stride);
					::memcpy(line1, line2, stride);
					::memcpy(line2, templine, stride);
				}
			}
		}

		void													OGL4Texture::Update(unsigned int level, const unsigned char *data, std::size_t size)
		{
			assert(data != nullptr && size != 0);

			GLCHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 4));
			GLCHECK(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
			GLCHECK(glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0));
			GLCHECK(glPixelStorei(GL_UNPACK_SKIP_ROWS, 0));
			GLCHECK(glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0));

			GLint previous = 0;
			GLCHECK(glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous));

			GLCHECK(glBindTexture(GL_TEXTURE_2D, this->mID));

			std::unique_ptr<unsigned char[]> dataFlipped(new unsigned char[size]);
			std::memcpy(dataFlipped.get(), data, size);
			FlipImage(this->mDesc, dataFlipped.get());

			if (this->mDesc.Format >= Texture::Format::DXT1 && this->mDesc.Format <= Texture::Format::LATC2)
			{
				GLCHECK(glCompressedTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, this->mDesc.Width, this->mDesc.Height, GL_UNSIGNED_BYTE, static_cast<GLsizei>(size), dataFlipped.get()));
			}
			else
			{
				GLenum format = GL_NONE;

				switch (this->mDesc.Format)
				{
					case Texture::Format::R8:
					case Texture::Format::R32F:
						format = GL_RED;
						break;
					case Texture::Format::RG8:
						format = GL_RG;
						break;
					case Texture::Format::RGBA8:
					case Texture::Format::RGBA16:
					case Texture::Format::RGBA16F:
					case Texture::Format::RGBA32F:
						format = GL_RGBA;
						break;
				}

				GLCHECK(glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, this->mDesc.Width, this->mDesc.Height, format, GL_UNSIGNED_BYTE, dataFlipped.get()));
			}

			GLCHECK(glBindTexture(GL_TEXTURE_2D, previous));
		}
		void													OGL4Texture::UpdateFromColorBuffer(void)
		{
			GLCHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, 0));
			GLCHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, this->mEffect->mDefaultFBO));
			GLCHECK(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->mID, 0));

#ifdef _DEBUG
			GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
			assert(status == GL_FRAMEBUFFER_COMPLETE);
#endif

			GLCHECK(glReadBuffer(GL_BACK));
			GLCHECK(glDrawBuffer(GL_COLOR_ATTACHMENT0));

			GLCHECK(glBlitFramebuffer(0, 0, this->mEffect->mEffectContext->mWidth, this->mEffect->mEffectContext->mHeight, 0, 0, this->mDesc.Width, this->mDesc.Height, GL_COLOR_BUFFER_BIT, GL_NEAREST));
		}
		void													OGL4Texture::UpdateFromDepthBuffer(void)
		{
			GLCHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, 0));
			GLCHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, this->mEffect->mDefaultFBO));
			GLCHECK(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->mID, 0));

			GLCHECK(glReadBuffer(GL_BACK));
			GLCHECK(glDrawBuffer(GL_COLOR_ATTACHMENT0));

			GLCHECK(glBlitFramebuffer(0, 0, this->mEffect->mEffectContext->mWidth, this->mEffect->mEffectContext->mHeight, 0, 0, this->mDesc.Width, this->mDesc.Height, GL_DEPTH_BUFFER_BIT, GL_NEAREST));
		}

		OGL4Constant::OGL4Constant(OGL4Effect *effect) : mEffect(effect)
		{
		}
		OGL4Constant::~OGL4Constant(void)
		{
		}

		const Effect::Constant::Description						OGL4Constant::GetDescription(void) const
		{
			return this->mDesc;
		}
		const Effect::Annotation								OGL4Constant::GetAnnotation(const std::string &name) const
		{
			auto it = this->mAnnotations.find(name);

			if (it == this->mAnnotations.end())
			{
				return Effect::Annotation();
			}

			return it->second;
		}
		void													OGL4Constant::GetValue(unsigned char *data, std::size_t size) const
		{
			size = std::min(size, this->mDesc.Size);

			const unsigned char *storage = this->mEffect->mUniformStorages[this->mBuffer].first + this->mBufferOffset;

			std::memcpy(data, storage, size);
		}
		void													OGL4Constant::SetValue(const unsigned char *data, std::size_t size)
		{
			size = std::min(size, this->mDesc.Size);

			unsigned char *storage = this->mEffect->mUniformStorages[this->mBuffer].first + this->mBufferOffset;

			if (std::memcmp(storage, data, size) == 0)
			{
				return;
			}

			std::memcpy(storage, data, size);

			this->mEffect->mUniformDirty = true;
		}

		OGL4Technique::OGL4Technique(OGL4Effect *effect) : mEffect(effect)
		{
		}
		OGL4Technique::~OGL4Technique(void)
		{
			for (auto &pass : this->mPasses)
			{
				GLCHECK(glDeleteProgram(pass.Program));
				GLCHECK(glDeleteFramebuffers(1, &pass.Framebuffer));
			}
		}

		const Effect::Technique::Description					OGL4Technique::GetDescription(void) const
		{
			return this->mDesc;
		}
		const Effect::Annotation								OGL4Technique::GetAnnotation(const std::string &name) const
		{
			auto it = this->mAnnotations.find(name);

			if (it == this->mAnnotations.end())
			{
				return Effect::Annotation();
			}

			return it->second;
		}

		bool													OGL4Technique::Begin(unsigned int &passes) const
		{
			passes = static_cast<unsigned int>(this->mPasses.size());

			this->mStateblock.Capture();

			GLCHECK(glBindVertexArray(this->mEffect->mDefaultVAO));
			GLCHECK(glBindVertexBuffer(0, this->mEffect->mDefaultVBO, 0, sizeof(float)));		

			for (GLuint i = 0, count = static_cast<GLuint>(this->mEffect->mSamplers.size()); i < count; ++i)
			{
				const auto &sampler = this->mEffect->mSamplers[i];
				const auto &texture = sampler->mTexture;

				GLCHECK(glActiveTexture(GL_TEXTURE0 + i));
				GLCHECK(glBindTexture(GL_TEXTURE_2D, sampler->mSRGB ? texture->mSRGBView : texture->mID));
				GLCHECK(glBindSampler(i, sampler->mID));
			}
			for (GLuint i = 0, count = static_cast<GLuint>(this->mEffect->mUniformBuffers.size()); i < count; ++i)
			{
				GLCHECK(glBindBufferBase(GL_UNIFORM_BUFFER, i, this->mEffect->mUniformBuffers[i]));
			}

			return true;
		}
		void													OGL4Technique::End(void) const
		{
			this->mStateblock.Apply();
		}
		void													OGL4Technique::RenderPass(unsigned int index) const
		{
			if (this->mEffect->mUniformDirty)
			{
				for (GLuint i = 0, count = static_cast<GLuint>(this->mEffect->mUniformBuffers.size()); i < count; ++i)
				{
					if (this->mEffect->mUniformBuffers[i] == 0)
					{
						continue;
					}

					GLCHECK(glBindBuffer(GL_UNIFORM_BUFFER, this->mEffect->mUniformBuffers[i]));
					GLCHECK(glBufferSubData(GL_UNIFORM_BUFFER, 0, this->mEffect->mUniformStorages[i].second, this->mEffect->mUniformStorages[i].first));
				}

				this->mEffect->mUniformDirty = false;
			}

			const Pass &pass = this->mPasses[index];

			GLCHECK(glViewport(0, 0, pass.ViewportWidth, pass.ViewportHeight));
			GLCHECK(glUseProgram(pass.Program));
			GLCHECK(glBindFramebuffer(GL_FRAMEBUFFER, pass.Framebuffer));
			GLCHECK(glEnableb(GL_FRAMEBUFFER_SRGB, pass.FramebufferSRGB));

			const GLfloat color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

			if (pass.Framebuffer == 0)
			{
				GLCHECK(glDrawBuffer(GL_BACK));
				GLCHECK(glClearBufferfv(GL_COLOR, 0, color));
			}
			else
			{
				GLCHECK(glDrawBuffers(8, pass.DrawBuffers));

				for (GLuint i = 0; i < 8; ++i)
				{
					if (pass.DrawBuffers[i] == GL_NONE)
					{
						continue;
					}

					GLCHECK(glClearBufferfv(GL_COLOR, i, color));
				}
			}

			GLCHECK(glDisable(GL_SCISSOR_TEST));
			GLCHECK(glFrontFace(GL_CCW));
			GLCHECK(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
			GLCHECK(glDisable(GL_CULL_FACE));
			GLCHECK(glColorMask(pass.ColorMaskR, pass.ColorMaskG, pass.ColorMaskB, pass.ColorMaskA));

			if (pass.Blend)
			{
				GLCHECK(glEnable(GL_BLEND));
				GLCHECK(glBlendFunc(pass.BlendFuncSrc, pass.BlendFuncDest));
				GLCHECK(glBlendEquationSeparate(pass.BlendEqColor, pass.BlendEqAlpha));
			}
			else
			{
				GLCHECK(glDisable(GL_BLEND));
			}

			if (pass.DepthMask)
			{
				GLCHECK(glEnable(GL_DEPTH_TEST));
				GLCHECK(glDepthMask(GL_TRUE));
				GLCHECK(glDepthFunc(pass.DepthTest ? pass.DepthFunc : GL_ALWAYS));
			}
			else
			{
				GLCHECK(glDepthMask(GL_FALSE));

				if (pass.DepthTest)
				{
					GLCHECK(glEnable(GL_DEPTH_TEST));
					GLCHECK(glDepthFunc(pass.DepthFunc));
				}
				else
				{
					GLCHECK(glDisable(GL_DEPTH_TEST));
				}
			}

			if (pass.StencilTest)
			{
				GLCHECK(glEnable(GL_STENCIL_TEST));
				GLCHECK(glStencilFunc(pass.StencilFunc, pass.StencilRef, pass.StencilReadMask));
				GLCHECK(glStencilOp(pass.StencilOpFail, pass.StencilOpZFail, pass.StencilOpZPass));
				GLCHECK(glStencilMask(pass.StencilMask));
			}
			else
			{
				GLCHECK(glDisable(GL_STENCIL_TEST));
			}

			GLCHECK(glDrawArrays(GL_TRIANGLES, 0, 3));
		}
	}

	// -----------------------------------------------------------------------------------------------------

	std::shared_ptr<Runtime>									CreateEffectRuntime(HDC hdc, HGLRC hglrc)
	{
		assert(hdc != nullptr && hglrc != nullptr);
		assert(wglGetCurrentDC() == hdc && wglGetCurrentContext() == hglrc);

		gl3wInit();

		if (!gl3wIsSupported(4, 3))
		{
			return nullptr;
		}

		return std::make_shared<OGL4EffectContext>(hdc, hglrc);
	}
}