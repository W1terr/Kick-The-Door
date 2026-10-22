#include "DoorModel.h"

namespace DoorModel
{
	namespace
	{
		struct Error : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		class Reader
		{
		public:
			Reader(std::span<const std::uint8_t> a_data, std::size_t a_pos = 0) :
				data(a_data), pos(a_pos) {}

			template <class T>
			T Get()
			{
				if (pos + sizeof(T) > data.size()) {
					throw Error("cut short");
				}
				T value;
				std::memcpy(&value, data.data() + pos, sizeof(T));
				pos += sizeof(T);
				return value;
			}

			void Skip(std::size_t a_bytes)
			{
				if (pos + a_bytes > data.size()) {
					throw Error("cut short");
				}
				pos += a_bytes;
			}

			std::string Text(std::size_t a_length)
			{
				if (pos + a_length > data.size()) {
					throw Error("cut short");
				}
				std::string text(reinterpret_cast<const char*>(data.data() + pos), a_length);
				pos += a_length;
				return text;
			}

			std::size_t Pos() const { return pos; }

		private:
			std::span<const std::uint8_t> data;
			std::size_t                   pos;
		};

		struct Block
		{
			std::string type;
			std::size_t offset{ 0 };
			std::size_t size{ 0 };
		};

		struct Nif
		{
			std::vector<Block>       blocks;
			std::vector<std::string> strings;

			const std::string& String(std::int32_t a_index) const
			{
				static const std::string none;
				return a_index >= 0 && a_index < static_cast<std::int32_t>(strings.size()) ? strings[static_cast<std::size_t>(a_index)] : none;
			}

			const Block* At(std::int32_t a_ref) const
			{
				return a_ref >= 0 && a_ref < static_cast<std::int32_t>(blocks.size()) ? &blocks[static_cast<std::size_t>(a_ref)] : nullptr;
			}
		};

		Nif ReadHeader(std::span<const std::uint8_t> a_data)
		{
			const auto end = std::find(a_data.begin(), a_data.end(), static_cast<std::uint8_t>('\n'));
			if (end == a_data.end()) {
				throw Error("not a nif");
			}
			Reader r(a_data, static_cast<std::size_t>(end - a_data.begin()) + 1);
			const auto version = r.Get<std::uint32_t>();
			if (version != 0x14020007) {
				throw Error("not a Skyrim nif");
			}
			r.Skip(1);  // endian
			r.Get<std::uint32_t>();  // user version
			const auto count = r.Get<std::uint32_t>();
			const auto bsVersion = r.Get<std::uint32_t>();
			for (int i = 0; i < (bsVersion >= 103 ? 4 : 3); ++i) {
				r.Skip(r.Get<std::uint8_t>());  // author, process script, export script (, max file path)
			}
			const auto typeCount = r.Get<std::uint16_t>();
			std::vector<std::string> types;
			for (std::uint16_t i = 0; i < typeCount; ++i) {
				types.push_back(r.Text(r.Get<std::uint32_t>()));
			}
			Nif nif;
			std::vector<std::uint16_t> typeIndex(count);
			for (auto& t : typeIndex) {
				t = r.Get<std::uint16_t>();
			}
			std::vector<std::uint32_t> sizes(count);
			for (auto& s : sizes) {
				s = r.Get<std::uint32_t>();
			}
			const auto stringCount = r.Get<std::uint32_t>();
			r.Get<std::uint32_t>();  // max length
			for (std::uint32_t i = 0; i < stringCount; ++i) {
				nif.strings.push_back(r.Text(r.Get<std::uint32_t>()));
			}
			r.Skip(4 * static_cast<std::size_t>(r.Get<std::uint32_t>()));  // groups
			std::size_t at = r.Pos();
			for (std::uint32_t i = 0; i < count; ++i) {
				const auto t = typeIndex[i] & 0x7FFF;
				nif.blocks.push_back({ t < types.size() ? types[t] : std::string(), at, sizes[i] });
				at += sizes[i];
			}
			if (at > a_data.size()) {
				throw Error("cut short");
			}
			return nif;
		}

		struct Turn
		{
			float        degrees{ 0.0f };
			RE::NiPoint3 axis;
			float        still{ 0.0f };  // time of the last key before it starts turning
		};

		// the largest turn of a NiTransformInterpolator's rotation keys from the first one, in the parent's space
		Turn KeyTurn(std::span<const std::uint8_t> a_data, const Nif& a_nif, const Block& a_interp)
		{
			Reader r(a_data, a_interp.offset);
			r.Skip(4 * 8);  // translation, rotation (quaternion), scale
			const auto* data = a_nif.At(r.Get<std::int32_t>());
			if (!data || data->type != "NiTransformData") {
				return {};
			}
			Reader d(a_data, data->offset);
			const auto rotationKeys = d.Get<std::uint32_t>();
			if (rotationKeys == 0) {
				return {};
			}
			const auto                           kind = d.Get<std::uint32_t>();
			Turn                                 result;
			std::vector<std::pair<float, float>> keys;  // time, angle from the first key (radians)
			if (kind == 4) {  // XYZ: three float curves; the axis whose curve changes most
				std::array<std::vector<std::pair<float, float>>, 3> curves;
				for (int axis = 0; axis < 3; ++axis) {
					const auto n = d.Get<std::uint32_t>();
					if (n == 0) {
						continue;
					}
					const auto floatKind = d.Get<std::uint32_t>();
					for (std::uint32_t k = 0; k < n; ++k) {
						const float time = d.Get<float>();
						const float value = d.Get<float>();
						if (floatKind == 2) {
							d.Skip(8);
						} else if (floatKind == 3) {
							d.Skip(12);
						}
						curves[static_cast<std::size_t>(axis)].emplace_back(time, value);
					}
				}
				float widest = 0.0f;
				for (int axis = 0; axis < 3; ++axis) {
					const auto& c = curves[static_cast<std::size_t>(axis)];
					float       span = 0.0f;
					for (const auto& [t, v] : c) {
						span = std::max(span, std::abs(v - c.front().second));
					}
					if (span > widest) {
						widest = span;
						keys.clear();
						for (const auto& [t, v] : c) {
							keys.emplace_back(t, v - c.front().second);
						}
						result.axis = { axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f };
					}
				}
			} else {  // quaternion keys
				std::array<float, 4> first{};
				float                widest = 0.0f;
				for (std::uint32_t k = 0; k < rotationKeys; ++k) {
					const float          time = d.Get<float>();
					std::array<float, 4> q{ d.Get<float>(), d.Get<float>(), d.Get<float>(), d.Get<float>() };  // w x y z
					if (kind == 3) {
						d.Skip(12);
					}
					if (k == 0) {
						first = q;
					}
					// q * conj(first): the turn from the first key, in the parent's space
					const float w = q[0] * first[0] + q[1] * first[1] + q[2] * first[2] + q[3] * first[3];
					const float x = -q[0] * first[1] + q[1] * first[0] - q[2] * first[3] + q[3] * first[2];
					const float y = -q[0] * first[2] + q[1] * first[3] + q[2] * first[0] - q[3] * first[1];
					const float z = -q[0] * first[3] - q[1] * first[2] + q[2] * first[1] + q[3] * first[0];
					const float sine = std::sqrt(x * x + y * y + z * z);
					const float angle = 2.0f * std::atan2(sine, std::abs(w));
					keys.emplace_back(time, angle);
					if (angle > widest && sine > 1e-6f) {
						widest = angle;
						result.axis = { x / sine, y / sine, z / sine };
					}
				}
			}
			for (const auto& [t, v] : keys) {
				result.degrees = std::max(result.degrees, std::abs(v) * 57.29578f);
			}
			result.still = keys.empty() ? 0.0f : keys.front().first;
			for (const auto& [t, v] : keys) {
				if (std::abs(v) > 0.0002f) {  // ~0.01 degrees
					break;
				}
				result.still = t;
			}
			return result;
		}
	}

	std::optional<Info> Parse(std::span<const std::uint8_t> a_nif)
	{
		const auto nif = ReadHeader(a_nif);
		for (const auto& block : nif.blocks) {
			if (block.type != "NiControllerSequence") {
				continue;
			}
			Reader r(a_nif, block.offset);
			const auto name = nif.String(r.Get<std::int32_t>());
			if (_stricmp(name.c_str(), "open") != 0) {
				continue;
			}
			const auto count = r.Get<std::uint32_t>();
			r.Get<std::uint32_t>();  // array grow by
			struct Controlled
			{
				std::int32_t interp;
				std::string  node;
			};
			std::vector<Controlled> controlled;
			for (std::uint32_t i = 0; i < count; ++i) {
				const auto interp = r.Get<std::int32_t>();
				r.Get<std::int32_t>();  // controller
				r.Skip(1);              // priority
				const auto node = nif.String(r.Get<std::int32_t>());
				r.Skip(4 * 4);  // property type, controller type, controller id, interpolator id
				controlled.push_back({ interp, node });
			}
			r.Get<float>();         // weight
			r.Get<std::int32_t>();  // text keys
			r.Get<std::uint32_t>(); // cycle type
			r.Get<float>();         // frequency
			const float start = r.Get<float>();
			const float stop = r.Get<float>();

			Info info;
			info.length = stop - start;
			for (const auto& c : controlled) {
				const auto* interp = nif.At(c.interp);
				if (!interp || interp->type != "NiTransformInterpolator") {
					continue;
				}
				const auto turn = KeyTurn(a_nif, nif, *interp);
				if (turn.degrees > kMinTurn) {
					info.parts.push_back({ c.node, turn.degrees, turn.axis, std::max(turn.still - start, 0.0f) });
				}
			}
			return info;
		}
		return std::nullopt;
	}

	std::optional<Info> Get(RE::TESObjectREFR* a_door)
	{
		static std::mutex                                                 lock;
		static std::unordered_map<std::string, std::optional<Info>>      cache;
		auto* const base = a_door ? a_door->GetBaseObject() : nullptr;
		auto* const model = base ? base->As<RE::TESModel>() : nullptr;
		const char* path = model ? model->GetModel() : nullptr;
		if (!path || !*path) {
			return std::nullopt;
		}
		std::string file = path;
		std::ranges::replace(file, '/', '\\');
		if (_strnicmp(file.c_str(), "meshes\\", 7) != 0) {
			file = "Meshes\\" + file;
		}
		std::scoped_lock guard(lock);
		if (const auto it = cache.find(file); it != cache.end()) {
			return it->second;
		}
		std::optional<Info> info;
		try {
			RE::BSResourceNiBinaryStream stream(file);
			if (!stream.good() || !stream.stream) {
				throw Error("can't open it");
			}
			std::vector<std::uint8_t> bytes(stream.stream->totalSize);
			if (bytes.empty() || !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::uint32_t>(bytes.size()))) {
				throw Error("can't read it");
			}
			info = Parse(bytes);
			if (info) {
				std::string parts;
				for (const auto& p : info->parts) {
					parts += std::format("'{}' {:.0f} deg about ({:.2f} {:.2f} {:.2f}) from {:.2f} s; ", p.name, p.turn, p.axis.x, p.axis.y, p.axis.z, p.start);
				}
				logs::info("[swing] {}: Open {:.2f} s, turning part(s): {}", file, info->length, parts.empty() ? "none" : parts);
			} else {
				logs::info("[swing] {}: no Open sequence in the model", file);
			}
		} catch (const std::exception& e) {
			logs::info("[swing] {}: {}", file, e.what());
		}
		cache.emplace(file, info);
		return info;
	}
}
