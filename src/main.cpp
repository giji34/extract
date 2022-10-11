#include <minecraft-file.hpp>
#include <getopt.h>
#include <hwm/task/task_queue.hpp>

#include <iostream>
#include <vector>

using namespace std;
using namespace mcfile;
using namespace mcfile::je;
using namespace mcfile::stream;
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
	return 0;
}
