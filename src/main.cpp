#include <minecraft-file.hpp>
#include <getopt.h>
#include <hwm/task/task_queue.hpp>

#include <iostream>
#include <vector>

using namespace std;
using namespace mcfile;
using namespace mcfile::je;
using namespace mcfile::stream;
using namespace mcfile::nbt;
namespace fs = std::filesystem;

static void ProcessChunk(World w, int rx, int rz, int cx, int cz, int by0, fs::path tmp) {
	auto region = w.region(rx, rz);
	auto chunk = region->writableChunkAt(cx, cz);
	auto dirt = make_shared<Block const>("minecraft:dirt");
	SetBlockOptions op;
	op.fRemoveTileEntity = true;
	for (int y = -63; y <= by0; y++) {
		for (int x = chunk->minBlockX(); x <= chunk->maxBlockX(); x++) {
			for (int z = chunk->minBlockZ(); z <= chunk->maxBlockZ(); z++) {
				chunk->setBlockAt(x, y, z, dirt, op);
			}
		}
	}
	auto rfs = make_shared<FileOutputStream>(tmp / Region::GetDefaultCompressedChunkNbtFileName(cx, cz));
	chunk->write(*rfs);
}

int main(int argc, char* argv[]) {
	fs::path in;
	fs::path out;
	int bx0 = 1;
	int bx1 = -1;
	int bz0 = 1;
	int bz1 = -1;
	int by0 = 47;

	int opt;
	while ((opt = getopt(argc, argv, "i:o:x:X:y:z:Z:")) != -1) {
		switch (opt) {
		case 'i':
			in = fs::path(optarg);
			break;
		case 'o':
			out = fs::path(optarg);
			break;
		case 'x':
			if (sscanf(optarg, "%d", &bx0) != 1) {
				return -1;
			}
			break;
		case 'X':
			if (sscanf(optarg, "%d", &bx1) != 1) {
				return -1;
			}
			break;
		case 'z':
			if (sscanf(optarg, "%d", &bz0) != 1) {
				return -1;
			}
			break;
		case 'Z':
			if (sscanf(optarg, "%d", &bz1) != 1) {
				return -1;
			}
			break;
		case 'y':
			if (sscanf(optarg, "%d", &by0) != 1) {
				return -1;
			}
			break;
		default:
			return -1;
		}
	}

	int cx0 = Coordinate::ChunkFromBlock(bx0);
	int cz0 = Coordinate::ChunkFromBlock(bz0);
	int cx1 = Coordinate::ChunkFromBlock(bx1);
	int cz1 = Coordinate::ChunkFromBlock(bz1);

	int rx0 = Coordinate::RegionFromBlock(bx0);
	int rz0 = Coordinate::RegionFromBlock(bz0);
	int rx1 = Coordinate::RegionFromBlock(bx1);
	int rz1 = Coordinate::RegionFromBlock(bz1);

	for (auto sub : { "region", "data", "entities" }) {
		fs::remove_all(out / sub);
		fs::create_directories(out / sub);
	}
	World w(out);

	for (auto it : fs::directory_iterator(in / "data")) {
		if (!it.is_regular_file()) {
			continue;
		}
		fs::copy_file(it.path(), out / "data" / it.path().filename(), fs::copy_options::overwrite_existing);
	}

	hwm::task_queue queue(thread::hardware_concurrency());

	for (int rx = rx0; rx <= rx1; rx++) {
		for (int rz = rz0; rz <= rz1; rz++) {
			auto fileName = Region::GetDefaultRegionFileName(rx, rz);
			if (!fs::exists(in / "region" / fileName)) {
				continue;
			}
			fs::copy_file(in / "region" / fileName, out / "region" / fileName, fs::copy_options::overwrite_existing);

			if (fs::exists(in / "entities" / fileName)) {
				fs::copy_file(in / "entities" / fileName, out / "entities" / fileName, fs::copy_options::overwrite_existing);
			}

			vector<future<void>> futures;

			auto region = w.region(rx, rz);
			auto tmpRegion = File::CreateTempDir(fs::temp_directory_path());
			region->exportAllToCompressedNbt(*tmpRegion);
			auto tmpEntities = File::CreateTempDir(fs::temp_directory_path());
			auto entities = Region::MakeRegion(region->entitiesRegionFilePath());
			if (fs::exists(entities->fFilePath)) {
				entities->exportAllToCompressedNbt(*tmpEntities);
			}

			for (int cx = region->minChunkX(); cx <= region->maxChunkX(); cx++) {
				for (int cz = region->minChunkZ(); cz <= region->maxChunkZ(); cz++) {
					if (cx0 <= cx && cx <= cx1 && cz0 <= cz && cz <= cz1) {
						futures.push_back(queue.enqueue(ProcessChunk, w, rx, rz, cx, cz, by0, *tmpRegion));
					} else {
						fs::remove(*tmpRegion / Region::GetDefaultCompressedChunkNbtFileName(cx, cz));
						fs::remove(*tmpEntities / Region::GetDefaultCompressedChunkNbtFileName(cx, cz));
					}
				}
			}

			for (auto& f : futures) {
				f.get();
			}

			Region::ConcatCompressedNbt(rx, rz, *tmpRegion, out / "region" / fileName);
			Region::ConcatCompressedNbt(rx, rz, *tmpEntities, out / "entities" / fileName);
			fs::remove_all(*tmpRegion);
			fs::remove_all(*tmpEntities);
		}
	}

	if (fs::exists(out / "level.dat")) {
		auto input = make_shared<GzFileInputStream>(out / "level.dat");
		auto root = CompoundTag::Read(input, mcfile::Endian::Big);
		auto data = root->compoundTag("Data");
		auto worldGenSettings = data->compoundTag("WorldGenSettings");
		worldGenSettings->set("seed", make_shared<LongTag>(123));
		auto dimensions = worldGenSettings->compoundTag("dimensions");

		auto overworld = make_shared<CompoundTag>();
		dimensions->set("minecraft:overworld", overworld);

		auto generator = make_shared<CompoundTag>();
		overworld->set("generator", generator);
		overworld->set("type", make_shared<StringTag>("minecraft:overworld"));

		auto settings = make_shared<CompoundTag>();
		generator->set("settings", settings);
		generator->set("type", make_shared<StringTag>("minecraft:flat"));

		settings->set("biome", make_shared<StringTag>("minecraft:plains"));
		settings->set("features", make_shared<ByteTag>(0));
		settings->set("lake", make_shared<ByteTag>(0));
		auto structureOverrides = make_shared<ListTag>(Tag::Type::String);
		settings->set("structure_overrides", structureOverrides);
		structureOverrides->push_back(make_shared<StringTag>("minecraft:strongholds"));
		structureOverrides->push_back(make_shared<StringTag>("minecraft:villages"));
		auto layers = make_shared<ListTag>(Tag::Type::Compound);
		settings->set("layers", layers);
		auto bedrock = make_shared<CompoundTag>();
		bedrock->set("block", make_shared<StringTag>("minecraft:bedrock"));
		bedrock->set("height", make_shared<IntTag>(1));
		auto dirt = make_shared<CompoundTag>();
		dirt->set("block", make_shared<StringTag>("minecraft:dirt"));
		dirt->set("height", make_shared<IntTag>(125));
		auto grassBlock = make_shared<CompoundTag>();
		grassBlock->set("block", make_shared<StringTag>("minecraft:grass_block"));
		grassBlock->set("height", make_shared<IntTag>(1));
		layers->push_back(bedrock);
		layers->push_back(dirt);
		layers->push_back(grassBlock);

		auto output = make_shared<GzFileOutputStream>(out / "level.dat");
		CompoundTag::Write(*root, output, Endian::Big);
	}

	return 0;
}
