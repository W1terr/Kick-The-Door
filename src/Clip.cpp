#include "Clip.h"

#include "Forms.h"
#include "MathUtil.h"
#include "Settings.h"

#include <nlohmann/json.hpp>
#include <random>

namespace Clip
{
	namespace
	{
		using RE::NiMatrix3;
		using RE::NiPoint3;

		// the vanilla character skeleton (meshes\actors\character\character assets\skeleton.hkx), in its bone order:
		// animations made on it address bones by these indices
		constexpr std::array<const char*, 99> kVanillaBones{
			"NPC Root [Root]",
			"x_NPC LookNode [Look]",
			"x_NPC Translate [Pos ]",
			"x_NPC Rotate [Rot ]",
			"NPC COM [COM ]",
			"NPC Pelvis [Pelv]",
			"NPC L Thigh [LThg]",
			"NPC L Calf [LClf]",
			"NPC L Foot [Lft ]",
			"NPC R Thigh [RThg]",
			"NPC R Calf [RClf]",
			"NPC R Foot [Rft ]",
			"SkirtRBone01",
			"SkirtRBone02",
			"SkirtRBone03",
			"SkirtLBone01",
			"SkirtLBone02",
			"SkirtLBone03",
			"SkirtBBone01",
			"SkirtBBone02",
			"SkirtBBone03",
			"SkirtFBone01",
			"SkirtFBone02",
			"SkirtFBone03",
			"NPC Spine [Spn0]",
			"NPC Spine1 [Spn1]",
			"NPC Spine2 [Spn2]",
			"NPC L Clavicle [LClv]",
			"NPC L UpperArm [LUar]",
			"NPC L Forearm [LLar]",
			"NPC R Clavicle [RClv]",
			"NPC R UpperArm [RUar]",
			"NPC R Forearm [RLar]",
			"AnimObjectA",
			"AnimObjectB",
			"NPC Neck [Neck]",
			"NPC Head [Head]",
			"NPCEyeBone",
			"NPC L Hand [LHnd]",
			"NPC R Hand [RHnd]",
			"AnimObjectL",
			"AnimObjectR",
			"Shield",
			"Weapon",
			"NPC L Pauldron",
			"NPC R Pauldron",
			"MagicEffectsNode",
			"NPC L MagicNode [LMag]",
			"NPC R MagicNode [RMag]",
			"NPC Head MagicNode [Hmag]",
			"NPC L Toe0 [LToe]",
			"NPC R Toe0 [RToe]",
			"NPC L ForearmTwist1 [LLt1]",
			"NPC L ForearmTwist2 [LLt2]",
			"NPC L UpperarmTwist1 [LUt1]",
			"NPC L UpperarmTwist2 [LUt2]",
			"NPC R ForearmTwist1 [RLt1]",
			"NPC R ForearmTwist2 [RLt2]",
			"NPC R UpperarmTwist1 [RUt1]",
			"NPC R UpperarmTwist2 [RUt2]",
			"Quiver",
			"WeaponAxe",
			"WeaponBack",
			"WeaponBow",
			"WeaponDagger",
			"WeaponMace",
			"WeaponSword",
			"NPC L Finger00 [LF00]",
			"NPC L Finger01 [LF01]",
			"NPC L Finger02 [LF02]",
			"NPC L Finger10 [LF10]",
			"NPC L Finger11 [LF11]",
			"NPC L Finger12 [LF12]",
			"NPC L Finger20 [LF20]",
			"NPC L Finger21 [LF21]",
			"NPC L Finger22 [LF22]",
			"NPC L Finger30 [LF30]",
			"NPC L Finger31 [LF31]",
			"NPC L Finger32 [LF32]",
			"NPC L Finger40 [LF40]",
			"NPC L Finger41 [LF41]",
			"NPC L Finger42 [LF42]",
			"NPC R Finger00 [RF00]",
			"NPC R Finger01 [RF01]",
			"NPC R Finger02 [RF02]",
			"NPC R Finger10 [RF10]",
			"NPC R Finger11 [RF11]",
			"NPC R Finger12 [RF12]",
			"NPC R Finger20 [RF20]",
			"NPC R Finger21 [RF21]",
			"NPC R Finger22 [RF22]",
			"NPC R Finger30 [RF30]",
			"NPC R Finger31 [RF31]",
			"NPC R Finger32 [RF32]",
			"NPC R Finger40 [RF40]",
			"NPC R Finger41 [RF41]",
			"NPC R Finger42 [RF42]",
			"Camera3rd [Cam3]",
			"Camera Control"
		};

		struct Key
		{
			NiPoint3 t;
			float    x{ 0.0f }, y{ 0.0f }, z{ 0.0f }, w{ 1.0f };
		};

		struct Data
		{
			std::vector<std::string>      bones;   // per track, empty = not applied
			std::vector<std::vector<Key>> frames;  // [frame][track]
			float                         frameDuration{ 1.0f / 30.0f };
		};

		struct Info
		{
			std::string format;  // "spline compressed" / "uncompressed"
			float       duration{ 0.0f };
			int         frames{ 0 };
			int         tracks{ 0 };
			int         bones{ 0 };  // tracks that found a bone name
			float       swing{ 0.0f };
			float       impact{ 0.0f };
			bool        swingMarked{ false };
			bool        impactMarked{ false };
		};

		struct Animation
		{
			std::string file;  // "Pack/kick.hkx"
			Data        data;
			Info        info;
		};

		// config.json conditions: unset = either
		struct Conditions
		{
			std::optional<bool>            female;
			std::optional<bool>            weaponDrawn;
			std::optional<bool>            sneaking;
			std::optional<bool>            lockedDoor;
			std::unordered_set<RE::FormID> races;  // empty = any
			bool                           racesListed{ false };

			bool Match(const Situation& a_s) const
			{
				return (!female || *female == a_s.female) && (!weaponDrawn || *weaponDrawn == a_s.weaponDrawn) &&
				       (!sneaking || *sneaking == a_s.sneaking) && (!lockedDoor || *lockedDoor == a_s.lockedDoor) &&
				       (!racesListed || races.contains(a_s.race));
			}
		};

		struct AnimationPack
		{
			PackInfo             info;
			Conditions           conditions;
			std::optional<float> swing;  // override the files' markers
			std::optional<float> impact;
			bool                 keepArms{ true };
			std::vector<int>     animations;
		};

		std::vector<Animation>     animations;
		std::vector<AnimationPack> packs;
		std::vector<PackInfo>      packInfos;

		struct Error : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		// ---- Havok packfile (hk_2010, 64 bit) ----------------------------------------------------------------------

		class Pack
		{
		public:
			explicit Pack(std::vector<std::uint8_t> a_bytes) :
				bytes(std::move(a_bytes))
			{
				if (bytes.size() < 0x40 || Raw<std::uint32_t>(0) != 0x57E0E057 || Raw<std::uint32_t>(4) != 0x10C0C010) {
					throw Error("not a Havok packfile");
				}
				if (bytes[0x10] != 8) {
					throw Error("32 bit (Skyrim LE) file: export it as Skyrim SE");
				}
				if (const auto version = Raw<std::int32_t>(0x0C); version != 8) {
					throw Error(std::format("packfile version {} (Skyrim SE files are version 8)", version));
				}
				const auto sections = Raw<std::int32_t>(0x14);
				std::size_t classes = 0;
				bool        found = false;
				for (std::int32_t i = 0; i < sections; ++i) {
					const std::size_t header = 0x40 + 0x30 * static_cast<std::size_t>(i);
					const std::string name(reinterpret_cast<const char*>(&At(header, 20)), strnlen(reinterpret_cast<const char*>(&At(header, 20)), 19));
					if (name == "__classnames__") {
						classes = static_cast<std::size_t>(Raw<std::int32_t>(header + 0x14));
					} else if (name == "__data__") {
						data = static_cast<std::size_t>(Raw<std::int32_t>(header + 0x14));
						for (int k = 0; k < 6; ++k) {
							offsets[k] = Raw<std::int32_t>(header + 0x18 + 4 * k);
						}
						found = true;
					}
				}
				if (!found) {
					throw Error("no data section");
				}
				for (auto p = offsets[0]; p + 8 <= offsets[1]; p += 8) {
					const auto src = Raw<std::int32_t>(data + p);
					if (src != -1) {
						local[src] = Raw<std::int32_t>(data + p + 4);
					}
				}
				for (auto p = offsets[1]; p + 12 <= offsets[2]; p += 12) {
					const auto src = Raw<std::int32_t>(data + p);
					if (src != -1) {
						global[src] = Raw<std::int32_t>(data + p + 8);
					}
				}
				for (auto p = offsets[2]; p + 12 <= offsets[3]; p += 12) {
					const auto object = Raw<std::int32_t>(data + p);
					if (object != -1) {
						const auto nameAt = classes + static_cast<std::size_t>(Raw<std::int32_t>(data + p + 8));
						objects.emplace_back(object, CString(nameAt));
					}
				}
			}

			std::optional<std::int32_t> Find(std::string_view a_class) const
			{
				for (const auto& [object, name] : objects) {
					if (name == a_class) {
						return object;
					}
				}
				return std::nullopt;
			}

			template <class T>
			T Get(std::int32_t a_offset) const
			{
				return Raw<T>(data + static_cast<std::size_t>(a_offset));
			}

			// where a pointer member points to (-1: null)
			std::int32_t Pointer(std::int32_t a_member) const
			{
				if (const auto it = local.find(a_member); it != local.end()) {
					return it->second;
				}
				if (const auto it = global.find(a_member); it != global.end()) {
					return it->second;
				}
				return -1;
			}

			std::string String(std::int32_t a_member) const
			{
				const auto at = Pointer(a_member);
				return at < 0 ? std::string() : CString(data + static_cast<std::size_t>(at));
			}

			// hkArray member: data offset and element count
			std::pair<std::int32_t, std::int32_t> Array(std::int32_t a_member) const
			{
				return { Pointer(a_member), Get<std::int32_t>(a_member + 8) };
			}

			std::span<const std::uint8_t> Bytes(std::int32_t a_offset, std::size_t a_count) const
			{
				return { &At(data + static_cast<std::size_t>(a_offset), a_count), a_count };
			}

		private:
			const std::uint8_t& At(std::size_t a_offset, std::size_t a_count) const
			{
				if (a_offset + a_count > bytes.size() || a_offset + a_count < a_offset) {
					throw Error("file is cut short or damaged");
				}
				return bytes[a_offset];
			}

			template <class T>
			T Raw(std::size_t a_offset) const
			{
				T value;
				std::memcpy(&value, &At(a_offset, sizeof(T)), sizeof(T));
				return value;
			}

			std::string CString(std::size_t a_offset) const
			{
				std::string text;
				for (std::size_t i = a_offset; i < bytes.size() && bytes[i]; ++i) {
					text.push_back(static_cast<char>(bytes[i]));
				}
				return text;
			}

			std::vector<std::uint8_t>                       bytes;
			std::size_t                                     data{ 0 };
			std::array<std::int32_t, 6>                     offsets{};
			std::unordered_map<std::int32_t, std::int32_t>  local;
			std::unordered_map<std::int32_t, std::int32_t>  global;
			std::vector<std::pair<std::int32_t, std::string>> objects;
		};

		// ---- hkaSplineCompressedAnimation ----------------------------------------------------------------------------

		class Reader
		{
		public:
			explicit Reader(std::span<const std::uint8_t> a_buffer, std::size_t a_pos) :
				buffer(a_buffer), pos(a_pos) {}

			void Align(std::size_t a_n) { pos = (pos + a_n - 1) / a_n * a_n; }

			const std::uint8_t* Take(std::size_t a_count)
			{
				if (pos + a_count > buffer.size()) {
					throw Error("animation data is cut short");
				}
				const auto* at = buffer.data() + pos;
				pos += a_count;
				return at;
			}

			std::uint8_t  U8() { return *Take(1); }
			std::uint16_t U16()
			{
				std::uint16_t v;
				std::memcpy(&v, Take(2), 2);
				return v;
			}
			float F32()
			{
				float v;
				std::memcpy(&v, Take(4), 4);
				return v;
			}

		private:
			std::span<const std::uint8_t> buffer;
			std::size_t                   pos;
		};

		constexpr std::array<std::size_t, 6> kRotationSize{ 4, 5, 6, 3, 2, 16 };
		constexpr std::array<std::size_t, 6> kRotationAlign{ 4, 1, 2, 1, 2, 4 };

		int FindSpan(int a_degree, float a_value, int a_count, const std::uint8_t* a_knots)
		{
			if (a_value >= a_knots[a_count]) {
				return a_count - 1;
			}
			int low = a_degree;
			int high = a_count;
			int mid = (low + high) / 2;
			for (int guard = 0; guard < 64 && (a_value < a_knots[mid] || a_value >= a_knots[mid + 1]); ++guard) {
				if (a_value < a_knots[mid]) {
					high = mid;
				} else {
					low = mid;
				}
				mid = (low + high) / 2;
			}
			return mid;
		}

		// point of the B-spline at a_frame (The NURBS Book A2.2 basis, as Havok's decompressor)
		template <std::size_t N>
		std::array<float, N> Spline(int a_span, int a_degree, float a_frame, const std::uint8_t* a_knots, const std::vector<std::array<float, N>>& a_points)
		{
			std::array<float, 5> basis{ 1.0f };
			std::array<float, 5> left{};
			std::array<float, 5> right{};
			for (int j = 1; j <= a_degree; ++j) {
				left[j] = a_frame - a_knots[a_span + 1 - j];
				right[j] = a_knots[a_span + j] - a_frame;
				float saved = 0.0f;
				for (int r = 0; r < j; ++r) {
					const float denominator = right[r + 1] + left[j - r];
					const float temp = denominator != 0.0f ? basis[r] / denominator : 0.0f;
					basis[r] = saved + right[r + 1] * temp;
					saved = left[j - r] * temp;
				}
				basis[j] = saved;
			}
			std::array<float, N> result{};
			for (int k = 0; k <= a_degree; ++k) {
				const auto index = static_cast<std::size_t>(std::clamp(a_span - a_degree + k, 0, static_cast<int>(a_points.size()) - 1));
				for (std::size_t c = 0; c < N; ++c) {
					result[c] += a_points[index][c] * basis[k];
				}
			}
			return result;
		}

		std::array<float, 4> Quaternion(const std::uint8_t* a_raw, int a_type)
		{
			std::array<float, 3> values{};
			int                  shift = 0;
			bool                 invert = false;
			if (a_type == 1) {  // THREECOMP40
				std::uint64_t c = 0;
				for (int i = 0; i < 5; ++i) {
					c |= static_cast<std::uint64_t>(a_raw[i]) << (8 * i);
				}
				for (int i = 0; i < 3; ++i) {
					values[i] = (static_cast<int>((c >> (12 * i)) & 0xFFF) - 2047) * 0.000345436f;
				}
				shift = static_cast<int>((c >> 36) & 3);
				invert = ((c >> 38) & 1) != 0;
			} else if (a_type == 2) {  // THREECOMP48
				std::uint16_t v[3];
				std::memcpy(v, a_raw, 6);
				for (int i = 0; i < 3; ++i) {
					values[i] = (static_cast<int>(v[i] & 0x7FFF) - 0x3FFF) * 0.000043161f;
				}
				shift = ((v[1] >> 14) & 2) | ((v[0] >> 15) & 1);
				invert = ((v[2] >> 15) & 1) != 0;
			} else if (a_type == 5) {  // uncompressed
				std::array<float, 4> q;
				std::memcpy(q.data(), a_raw, 16);
				return q;
			} else {
				throw Error(std::format("rotation quantization {} is not supported (use THREECOMP40 / 48 or uncompressed)", a_type));
			}
			float w = std::sqrt(std::max(0.0f, 1.0f - values[0] * values[0] - values[1] * values[1] - values[2] * values[2]));
			if (invert) {
				w = -w;
			}
			std::array<float, 4> q{};
			for (int i = 0, k = 0; i < 4; ++i) {
				q[i] = i == shift ? w : values[k++];
			}
			return q;
		}

		void ReadVector(Reader& a_reader, std::uint8_t a_types, int a_quantization, float a_frame, std::array<float, 3>& a_value)
		{
			if (a_types & 0xF0) {
				const int  items = a_reader.U16();
				const int  degree = a_reader.U8();
				const auto knots = a_reader.Take(static_cast<std::size_t>(items + degree + 2));
				a_reader.Align(4);
				struct Range
				{
					int   component;
					float low, high;
				};
				std::vector<Range> ranges;
				for (int i = 0; i < 3; ++i) {
					if (a_types & (0x10 << i)) {
						const float low = a_reader.F32();
						const float high = a_reader.F32();
						ranges.push_back({ i, low, high });
					} else if (a_types & (0x01 << i)) {
						a_value[i] = a_reader.F32();
					}
				}
				const std::size_t size = a_quantization == 0 ? 1 : 2;
				if (size == 2) {
					a_reader.Align(2);
				}
				std::vector<std::array<float, 3>> points(static_cast<std::size_t>(items + 1));
				for (auto& point : points) {
					for (std::size_t r = 0; r < ranges.size(); ++r) {
						const float q = size == 1 ? a_reader.U8() / 255.0f : a_reader.U16() / 65535.0f;
						point[r] = ranges[r].low + (ranges[r].high - ranges[r].low) * q;
					}
				}
				a_reader.Align(4);
				if (!ranges.empty()) {
					const auto v = Spline<3>(FindSpan(degree, a_frame, items + 1, knots), degree, a_frame, knots, points);
					for (std::size_t r = 0; r < ranges.size(); ++r) {
						a_value[static_cast<std::size_t>(ranges[r].component)] = v[r];
					}
				}
			} else if (a_types & 0x0F) {
				for (int i = 0; i < 3; ++i) {
					if (a_types & (0x01 << i)) {
						a_value[i] = a_reader.F32();
					}
				}
			}
		}

		std::array<float, 4> ReadRotation(Reader& a_reader, std::uint8_t a_types, int a_quantization, float a_frame)
		{
			if (a_quantization < 0 || a_quantization > 5) {
				throw Error("bad rotation quantization");
			}
			const auto type = static_cast<std::size_t>(a_quantization);
			if (a_types & 0xF0) {
				const int  items = a_reader.U16();
				const int  degree = a_reader.U8();
				const auto knots = a_reader.Take(static_cast<std::size_t>(items + degree + 2));
				a_reader.Align(kRotationAlign[type]);
				std::vector<std::array<float, 4>> points;
				for (int k = 0; k <= items; ++k) {
					auto q = Quaternion(a_reader.Take(kRotationSize[type]), a_quantization);
					if (!points.empty()) {
						const auto& prev = points.back();
						if (q[0] * prev[0] + q[1] * prev[1] + q[2] * prev[2] + q[3] * prev[3] < 0.0f) {
							for (auto& c : q) {
								c = -c;
							}
						}
					}
					points.push_back(q);
				}
				auto        q = Spline<4>(FindSpan(degree, a_frame, items + 1, knots), degree, a_frame, knots, points);
				const float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
				for (auto& c : q) {
					c /= length > 1e-8f ? length : 1.0f;
				}
				return q;
			}
			if (a_types & 0x0F) {
				a_reader.Align(kRotationAlign[type]);
				return Quaternion(a_reader.Take(kRotationSize[type]), a_quantization);
			}
			return { 0.0f, 0.0f, 0.0f, 1.0f };
		}

		void DecodeSpline(const Pack& a_pack, std::int32_t a_anim, int a_tracks, Data& a_out)
		{
			const auto frames = a_pack.Get<std::int32_t>(a_anim + 0x38);
			const auto blocks = a_pack.Get<std::int32_t>(a_anim + 0x3C);
			const auto perBlock = a_pack.Get<std::int32_t>(a_anim + 0x40);
			const auto maskSize = a_pack.Get<std::int32_t>(a_anim + 0x44);
			a_out.frameDuration = a_pack.Get<float>(a_anim + 0x50);
			const auto [blockAt, blockCount] = a_pack.Array(a_anim + 0x58);
			const auto [dataAt, dataSize] = a_pack.Array(a_anim + 0x98);
			if (frames < 2 || blocks < 1 || perBlock < 2 || blockAt < 0 || blockCount < blocks || dataAt < 0 || dataSize <= 0) {
				throw Error("empty or damaged animation");
			}
			const auto buffer = a_pack.Bytes(dataAt, static_cast<std::size_t>(dataSize));
			std::vector<std::uint32_t> blockOffsets;
			for (int b = 0; b < blocks; ++b) {
				blockOffsets.push_back(a_pack.Get<std::uint32_t>(blockAt + 4 * b));
			}
			a_out.frames.assign(static_cast<std::size_t>(frames), std::vector<Key>(static_cast<std::size_t>(a_tracks)));
			for (int f = 0; f < frames; ++f) {
				const int   block = std::min(f / (perBlock - 1), blocks - 1);
				const float local = static_cast<float>(f - block * (perBlock - 1));
				const auto  start = static_cast<std::size_t>(blockOffsets[static_cast<std::size_t>(block)]);
				Reader      masks(buffer, start);
				Reader      reader(buffer, start + (static_cast<std::size_t>(maskSize) + 3) / 4 * 4);
				for (int t = 0; t < a_tracks; ++t) {
					const auto* mask = masks.Take(4);
					const int   quantization = mask[0];
					std::array<float, 3> position{ 0.0f, 0.0f, 0.0f };
					ReadVector(reader, mask[1], quantization & 3, local, position);
					reader.Align(4);
					const auto rotation = ReadRotation(reader, mask[2], (quantization >> 2) & 0xF, local);
					reader.Align(4);
					std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
					ReadVector(reader, mask[3], (quantization >> 6) & 3, local, scale);
					reader.Align(4);
					auto& key = a_out.frames[static_cast<std::size_t>(f)][static_cast<std::size_t>(t)];
					key.t = { position[0], position[1], position[2] };
					key.x = rotation[0];
					key.y = rotation[1];
					key.z = rotation[2];
					key.w = rotation[3];
				}
			}
		}

		void DecodeUncompressed(const Pack& a_pack, std::int32_t a_anim, int a_tracks, float a_duration, Data& a_out)
		{
			const auto [at, count] = a_pack.Array(a_anim + 0x38);
			if (at < 0 || a_tracks <= 0 || count < a_tracks * 2) {
				throw Error("empty or damaged animation");
			}
			const int frames = count / a_tracks;
			a_out.frameDuration = a_duration / static_cast<float>(frames - 1);
			a_out.frames.assign(static_cast<std::size_t>(frames), std::vector<Key>(static_cast<std::size_t>(a_tracks)));
			for (int f = 0; f < frames; ++f) {
				for (int t = 0; t < a_tracks; ++t) {
					const auto base = at + 0x30 * (f * a_tracks + t);
					auto&      key = a_out.frames[static_cast<std::size_t>(f)][static_cast<std::size_t>(t)];
					key.t = { a_pack.Get<float>(base), a_pack.Get<float>(base + 4), a_pack.Get<float>(base + 8) };
					key.x = a_pack.Get<float>(base + 0x10);
					key.y = a_pack.Get<float>(base + 0x14);
					key.z = a_pack.Get<float>(base + 0x18);
					key.w = a_pack.Get<float>(base + 0x1C);
				}
			}
		}

		bool Same(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
		}

		void Read(const std::filesystem::path& a_path, Data& a_out, Info& a_info)
		{
			std::ifstream file(a_path, std::ios::binary);
			if (!file) {
				throw Error("can't be opened");
			}
			Pack pack(std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {}));

			auto anim = pack.Find("hkaSplineCompressedAnimation");
			bool spline = anim.has_value();
			if (!anim) {
				anim = pack.Find("hkaInterleavedUncompressedAnimation");
			}
			if (!anim) {
				throw Error("no animation in it (spline compressed or uncompressed expected)");
			}
			const float duration = pack.Get<float>(*anim + 0x14);
			const int   tracks = pack.Get<std::int32_t>(*anim + 0x18);
			if (tracks <= 0 || tracks > 1024 || !(duration > 0.0f)) {
				throw Error("empty animation");
			}

			// track -> bone: the binding's indices into the vanilla skeleton (none = in order); track names win if the
			// file has them
			std::vector<int> boneIndex(static_cast<std::size_t>(tracks));
			for (int t = 0; t < tracks; ++t) {
				boneIndex[static_cast<std::size_t>(t)] = t;
			}
			if (const auto binding = pack.Find("hkaAnimationBinding")) {
				const auto [at, count] = pack.Array(*binding + 0x20);
				if (at >= 0 && count == tracks) {
					for (int t = 0; t < tracks; ++t) {
						boneIndex[static_cast<std::size_t>(t)] = pack.Get<std::int16_t>(at + 2 * t);
					}
				}
			}
			a_out.bones.assign(static_cast<std::size_t>(tracks), {});
			const auto [annotationsAt, annotationTracks] = pack.Array(*anim + 0x28);
			for (int t = 0; t < tracks; ++t) {
				std::string name;
				if (annotationsAt >= 0 && annotationTracks == tracks) {
					name = pack.String(annotationsAt + 0x18 * t);
				}
				const int index = boneIndex[static_cast<std::size_t>(t)];
				if (name.empty() && index >= 0 && index < static_cast<int>(kVanillaBones.size())) {
					name = kVanillaBones[static_cast<std::size_t>(index)];
				}
				a_out.bones[static_cast<std::size_t>(t)] = name;
			}

			// markers: "Swing" and "Impact" ("HitFrame" too, the game's name for it) on any track
			for (int t = 0; annotationsAt >= 0 && t < annotationTracks; ++t) {
				const auto [notesAt, notes] = pack.Array(annotationsAt + 0x18 * t + 8);
				for (int n = 0; notesAt >= 0 && n < notes; ++n) {
					const float time = pack.Get<float>(notesAt + 0x10 * n);
					const auto  text = pack.String(notesAt + 0x10 * n + 8);
					if (Same(text, "impact") || Same(text, "hitframe")) {
						a_info.impact = time;
						a_info.impactMarked = true;
					} else if (Same(text, "swing")) {
						a_info.swing = time;
						a_info.swingMarked = true;
					}
				}
			}

			if (spline) {
				DecodeSpline(pack, *anim, tracks, a_out);
			} else {
				DecodeUncompressed(pack, *anim, tracks, duration, a_out);
			}
			a_info.format = spline ? "spline compressed" : "uncompressed";
			a_info.duration = duration;
			a_info.frames = static_cast<int>(a_out.frames.size());
			a_info.tracks = tracks;
			a_info.bones = static_cast<int>(std::ranges::count_if(a_out.bones, [](const std::string& a_name) { return !a_name.empty(); }));
			if (!a_info.impactMarked) {
				a_info.impact = duration * 0.42f;
			}
			if (!a_info.swingMarked) {
				a_info.swing = a_info.impact * 0.6f;
			}
		}

		// ---- playing it on the skeleton --------------------------------------------------------------------------------

		bool Skipped(std::string_view a_bone)
		{
			// the root (the game moves and turns the actor), helper and attachment nodes
			return a_bone.empty() || a_bone == "NPC Root [Root]" || a_bone.starts_with("x_") || a_bone.starts_with("Camera") ||
			       a_bone.starts_with("AnimObject") || a_bone.starts_with("Weapon") || a_bone == "Shield" || a_bone == "Quiver" ||
			       a_bone.find("Magic") != std::string_view::npos;
		}

		bool Arm(std::string_view a_bone)
		{
			for (const auto part : { "Clavicle"sv, "UpperArm"sv, "Upperarm"sv, "Forearm"sv, "Hand"sv, "Finger"sv, "Pauldron"sv }) {
				if (a_bone.find(part) != std::string_view::npos) {
					return true;
				}
			}
			return false;
		}

		struct Joint
		{
			RE::NiPointer<RE::NiAVObject> node;
			NiMatrix3                     baseRotate;
			NiPoint3                      baseTranslate;
			NiMatrix3                     writtenRotate;
			NiPoint3                      writtenTranslate;
			bool                          wrote{ false };
			bool                          arm{ false };
		};

		struct Player
		{
			RE::NiPointer<RE::NiAVObject> root;
			int                           animation{ -1 };   // the joints below are bound for this one
			std::vector<Joint>            joints;            // per track
			int                           convention{ -1 };  // -1 unknown, 0 as Havok's matrix, 1 transposed
		} player;

		NiMatrix3 Matrix(const Key& a_key, bool a_transposed)
		{
			const float x = a_key.x, y = a_key.y, z = a_key.z, w = a_key.w;
			NiMatrix3   m;
			m.entry[0][0] = 1 - 2 * (y * y + z * z);
			m.entry[0][1] = 2 * (x * y - z * w);
			m.entry[0][2] = 2 * (x * z + y * w);
			m.entry[1][0] = 2 * (x * y + z * w);
			m.entry[1][1] = 1 - 2 * (x * x + z * z);
			m.entry[1][2] = 2 * (y * z - x * w);
			m.entry[2][0] = 2 * (x * z - y * w);
			m.entry[2][1] = 2 * (y * z + x * w);
			m.entry[2][2] = 1 - 2 * (x * x + y * y);
			return a_transposed ? m.Transpose() : m;
		}

		Key Sample(const Data& a_clip, float a_time, std::size_t a_track)
		{
			const auto  last = a_clip.frames.size() - 1;
			const float f = std::clamp(a_time / a_clip.frameDuration, 0.0f, static_cast<float>(last));
			const auto  i = std::min(static_cast<std::size_t>(f), last);
			const auto  j = std::min(i + 1, last);
			const float u = f - static_cast<float>(i);
			const auto& a = a_clip.frames[i][a_track];
			auto        b = a_clip.frames[j][a_track];
			if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) {
				b.x = -b.x;
				b.y = -b.y;
				b.z = -b.z;
				b.w = -b.w;
			}
			Key k;
			k.t = a.t + (b.t - a.t) * u;
			k.x = a.x + (b.x - a.x) * u;
			k.y = a.y + (b.y - a.y) * u;
			k.z = a.z + (b.z - a.z) * u;
			k.w = a.w + (b.w - a.w) * u;
			const float length = std::sqrt(k.x * k.x + k.y * k.y + k.z * k.z + k.w * k.w);
			k.x /= length;
			k.y /= length;
			k.z /= length;
			k.w /= length;
			return k;
		}

		void Bind(RE::NiAVObject* a_root, int a_animation)
		{
			Release();
			const auto& clip = animations[static_cast<std::size_t>(a_animation)].data;
			player.root.reset(a_root);
			player.animation = a_animation;
			player.joints.assign(clip.bones.size(), {});
			int found = 0;
			for (std::size_t t = 0; t < clip.bones.size(); ++t) {
				const auto& name = clip.bones[t];
				if (Skipped(name)) {
					continue;
				}
				if (auto* node = a_root->GetObjectByName(RE::BSFixedString(name.c_str()))) {
					player.joints[t].node.reset(node);
					player.joints[t].arm = Arm(name);
					++found;
				}
			}
			if (Settings::Get().debugLog) {
				logs::info("Kick animation {}: {} of {} tracks found a bone on the player", animations[static_cast<std::size_t>(a_animation)].file, found,
					clip.bones.size());
			}
		}

		// Havok's quaternion should turn into the node's rotation as is; checked once against the animated pose of a
		// few bones (a kick starts from a standing pose), in case the game stores it the other way round
		void Detect(const Data& a_clip)
		{
			std::array<float, 2> error{};
			int                  compared = 0;
			for (std::size_t t = 0; t < player.joints.size(); ++t) {
				const auto& name = a_clip.bones[t];
				if (!player.joints[t].node || !(name.find("Thigh") != std::string::npos || name.find("Calf") != std::string::npos ||
													name.find("Spine") != std::string::npos || name.find("Neck") != std::string::npos)) {
					continue;
				}
				for (int c = 0; c < 2; ++c) {
					error[static_cast<std::size_t>(c)] += M::Angle(player.joints[t].node->local.rotate.Transpose() * Matrix(a_clip.frames[0][t], c == 1));
				}
				++compared;
			}
			player.convention = compared > 0 && error[1] < error[0] * 0.5f ? 1 : 0;
			logs::info("Kick animation vs the animated pose ({} bones): {:.0f} deg as is, {:.0f} deg transposed -> {}", compared,
				error[0] / M::kDegrees, error[1] / M::kDegrees, player.convention == 0 ? "as is" : "transposed");
		}

		// ---- animation packs -------------------------------------------------------------------------------------------

		std::string Utf8(const std::filesystem::path& a_path)
		{
			const auto text = a_path.u8string();
			return { text.begin(), text.end() };
		}

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		// config.json of a pack (comments allowed); unknown keys are ignored
		void ReadConfig(const std::filesystem::path& a_path, AnimationPack& a_pack)
		{
			std::ifstream file(a_path);
			const auto    j = nlohmann::json::parse(file, nullptr, true, true);
			if (!j.is_object()) {
				throw Error("not a json object");
			}
			auto& info = a_pack.info;
			info.name = j.value("name", info.name);
			info.author = j.value("author", info.author);
			info.priority = j.value("priority", info.priority);
			a_pack.keepArms = j.value("keepArmsWithWeapon", a_pack.keepArms);
			if (j.contains("swing") && j["swing"].is_number()) {
				a_pack.swing = j["swing"].get<float>();
			}
			if (j.contains("impact") && j["impact"].is_number()) {
				a_pack.impact = j["impact"].get<float>();
			}
			if (!j.contains("conditions")) {
				return;
			}
			const auto& c = j["conditions"];
			auto&       conditions = a_pack.conditions;
			auto        flag = [&](const char* a_key, std::optional<bool>& a_out, const char* a_yes, const char* a_no) {
				if (c.contains(a_key) && c[a_key].is_boolean()) {
					a_out = c[a_key].get<bool>();
					info.conditions.emplace_back(*a_out ? a_yes : a_no);
				}
			};
			if (c.contains("sex") && c["sex"].is_string()) {
				const auto sex = Lower(c["sex"].get<std::string>());
				if (sex == "female" || sex == "male") {
					conditions.female = sex == "female";
					info.conditions.emplace_back(*conditions.female ? "female" : "male");
				}
			}
			flag("weaponDrawn", conditions.weaponDrawn, "weapon drawn", "no weapon drawn");
			flag("sneaking", conditions.sneaking, "sneaking", "not sneaking");
			flag("lockedDoor", conditions.lockedDoor, "locked door", "unlocked door");
			if (c.contains("races") && c["races"].is_array()) {
				conditions.racesListed = true;
				for (const auto& entry : c["races"]) {
					if (!entry.is_string()) {
						continue;
					}
					const auto spec = entry.get<std::string>();
					if (auto* const race = Forms::Resolve(spec); race && race->Is(RE::FormType::Race)) {
						conditions.races.insert(race->GetFormID());
						info.races.push_back(spec);
					} else {
						info.errors.push_back(std::format("config.json: race '{}' not found", spec));
					}
				}
			}
		}

		void LoadPack(const std::filesystem::path& a_folder)
		{
			AnimationPack pack;
			auto& info = pack.info;
			info.folder = Utf8(a_folder.filename());
			info.name = info.folder;
			info.priority = 1;  // above the mod's own kick (config.json of "Default": 0)

			std::vector<std::filesystem::path> files;
			std::error_code                    ec;
			for (const auto& entry : std::filesystem::directory_iterator(a_folder, ec)) {
				const auto name = Lower(Utf8(entry.path().filename()));
				if (!entry.is_regular_file(ec)) {
					continue;
				}
				if (name == "config.json") {
					try {
						ReadConfig(entry.path(), pack);
					} catch (const std::exception& e) {
						info.errors.push_back(std::format("config.json: {}", e.what()));
					}
				} else if (name.ends_with(".hkx")) {
					files.push_back(entry.path());
				}
			}
			std::ranges::sort(files);

			for (const auto& path : files) {
				Animation animation;
				animation.file = std::format("{}/{}", info.folder, Utf8(path.filename()));
				try {
					Read(path, animation.data, animation.info);
				} catch (const std::exception& e) {
					info.errors.push_back(std::format("{}: {}", Utf8(path.filename()), e.what()));
					logs::error("Kick animation {}: {}", animation.file, e.what());
					continue;
				}
				const auto& a = animation.info;
				logs::info("Kick animation {}: {}, {:.2f} s, {} frames, {} tracks ({} bones), swing {:.2f} s{}, impact {:.2f} s{}", animation.file,
					a.format, a.duration, a.frames, a.tracks, a.bones, a.swing, a.swingMarked ? "" : " (default)", a.impact, a.impactMarked ? "" : " (default)");
				pack.animations.push_back(static_cast<int>(animations.size()));
				animations.push_back(std::move(animation));
			}
			if (files.empty()) {
				info.errors.emplace_back("no .hkx files");
			}
			info.animations = static_cast<int>(pack.animations.size());
			logs::info("Animation pack '{}' ({}): priority {}, {} animation(s){}", info.name, info.folder, info.priority, info.animations,
				info.errors.empty() ? "" : std::format(", {} problem(s)", info.errors.size()));
			packs.push_back(std::move(pack));
		}

		std::size_t PackOf(int a_animation)
		{
			for (std::size_t p = 0; p < packs.size(); ++p) {
				if (std::ranges::find(packs[p].animations, a_animation) != packs[p].animations.end()) {
					return p;
				}
			}
			return 0;
		}

		int Random(int a_count)
		{
			static std::mt19937 rng{ std::random_device{}() };
			return std::uniform_int_distribution<int>(0, a_count - 1)(rng);
		}
	}

	void Load()
	{
		Release();
		animations.clear();
		packs.clear();
		std::error_code                    ec;
		std::vector<std::filesystem::path> folders;
		for (const auto& entry : std::filesystem::directory_iterator(kFolder, ec)) {
			if (entry.is_directory(ec)) {
				folders.push_back(entry.path());
			}
		}
		if (ec && folders.empty()) {
			logs::error("{} is missing: the kick plays without an animation", kFolder);
		}
		std::ranges::sort(folders, [](const auto& a, const auto& b) { return Lower(Utf8(a.filename())) < Lower(Utf8(b.filename())); });
		for (const auto& folder : folders) {
			LoadPack(folder);
		}
		packInfos.clear();
		for (const auto& pack : packs) {
			packInfos.push_back(pack.info);
		}
	}

	std::optional<Choice> Pick(const Situation& a_situation, std::string_view a_pack)
	{
		std::vector<int> pool;
		if (a_pack != "auto") {
			for (const auto& pack : packs) {
				if (Lower(pack.info.folder) == Lower(std::string(a_pack))) {
					pool = pack.animations;
				}
			}
		}
		if (pool.empty()) {
			// the highest priority that matches; packs with the same priority are mixed
			std::optional<int> best;
			for (const auto& pack : packs) {
				if (pack.animations.empty() || !pack.conditions.Match(a_situation)) {
					continue;
				}
				if (!best || pack.info.priority > *best) {
					best = pack.info.priority;
					pool.clear();
				}
				if (pack.info.priority == *best) {
					pool.insert(pool.end(), pack.animations.begin(), pack.animations.end());
				}
			}
		}
		if (pool.empty()) {
			return std::nullopt;
		}
		const int   index = pool[static_cast<std::size_t>(Random(static_cast<int>(pool.size())))];
		const auto& info = animations[static_cast<std::size_t>(index)].info;
		const auto& pack = packs[PackOf(index)];
		Choice      choice;
		choice.animation = index;
		choice.duration = info.duration;
		choice.impact = pack.impact.value_or(info.impact);
		choice.swing = pack.swing ? *pack.swing : info.swingMarked || !pack.impact ? info.swing : choice.impact * 0.6f;
		choice.keepArms = pack.keepArms;
		if (Settings::Get().debugLog) {
			logs::info("Kick animation picked: {} (of {})", animations[static_cast<std::size_t>(index)].file, pool.size());
		}
		return choice;
	}

	const std::vector<PackInfo>& Packs() { return packInfos; }

	void Apply(RE::Actor* a_player, int a_animation, float a_time, float a_weight, bool a_keepArms)
	{
		auto* const root = a_player ? a_player->Get3D(false) : nullptr;
		if (!root || a_animation < 0 || a_animation >= static_cast<int>(animations.size())) {
			return;
		}
		const auto& clip = animations[static_cast<std::size_t>(a_animation)].data;
		if (root != player.root.get() || player.animation != a_animation) {
			Bind(root, a_animation);
		}
		// take back what the animation didn't overwrite since the last frame
		for (auto& joint : player.joints) {
			if (!joint.node) {
				continue;
			}
			if (joint.wrote && M::Same(joint.node->local.rotate, joint.writtenRotate)) {
				joint.node->local.rotate = joint.baseRotate;
				joint.node->local.translate = joint.baseTranslate;
			} else {
				joint.baseRotate = joint.node->local.rotate;
				joint.baseTranslate = joint.node->local.translate;
			}
			joint.wrote = false;
		}
		if (player.convention < 0) {
			Detect(clip);
		}
		const bool  transposed = player.convention == 1;
		const float weight = std::clamp(a_weight, 0.0f, 1.0f);
		for (std::size_t t = 0; t < player.joints.size(); ++t) {
			auto& joint = player.joints[t];
			if (!joint.node || (a_keepArms && joint.arm) || weight <= 0.0f) {
				continue;
			}
			const auto key = Sample(clip, a_time, t);
			const auto rotate = Matrix(key, transposed);
			joint.node->local.rotate = weight >= 1.0f ? rotate : M::Slerp(joint.baseRotate, rotate, weight);
			// translations as offsets from the clip's first frame: the player's own proportions stay
			joint.node->local.translate = joint.baseTranslate + (key.t - clip.frames[0][t].t) * weight;
			joint.writtenRotate = joint.node->local.rotate;
			joint.writtenTranslate = joint.node->local.translate;
			joint.wrote = true;
		}
		RE::NiUpdateData context{};
		root->Update(context);
	}

	void Release()
	{
		bool changed = false;
		for (auto& joint : player.joints) {
			if (joint.node && joint.wrote && M::Same(joint.node->local.rotate, joint.writtenRotate)) {
				joint.node->local.rotate = joint.baseRotate;
				joint.node->local.translate = joint.baseTranslate;
				changed = true;
			}
			joint.wrote = false;
		}
		if (changed && player.root) {
			RE::NiUpdateData context{};
			player.root->Update(context);
		}
		player.joints.clear();
		player.root.reset();
		player.animation = -1;
	}
}
