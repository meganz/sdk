#include "mega/sccloudraid/mega.h"
#include <stdio.h>

mtime_t currtime;

namespace mega::SCCR {

int main_sccr(int argc, char** argv)
{
	// the six source parts of the CloudRAIDed file (first one is the parity part)
	// serverid of the source server as listed in config / hash of the source part
	RaidPart parts[6] = {
		{ 1165, { 0xc3, 0xaa, 0x32, 0x61, 0x26, 0xdf, 0x99, 0x55, 0x4a, 0xb8, 0xc8, 0xaf, 0xd0, 0x7f, 0x1c, 0xf1 } },
		{ 2178, { 0xfe, 0xed, 0xb6, 0xd4, 0x74, 0x72, 0x62, 0xc6, 0xe5, 0x88, 0x00, 0x64, 0x3e, 0xfe, 0xb0, 0x73 } },
		{ 3123, { 0x08, 0x5d, 0x15, 0xc4, 0x09, 0xb4, 0x34, 0x9d, 0xa4, 0x27, 0x39, 0x54, 0xff, 0x15, 0x3a, 0x5d } },
		{ 6187, { 0x62, 0xb0, 0x7c, 0x02, 0x5c, 0x6d, 0x0a, 0xcd, 0x91, 0x21, 0x90, 0x4c, 0xb1, 0x5e, 0x65, 0xd2 } },
		{ 4183, { 0x2a, 0xe6, 0x4a, 0xf4, 0xbe, 0xfb, 0xb8, 0xf3, 0xf3, 0xf3, 0x27, 0x5b, 0x47, 0x5b, 0xed, 0x85 } },
		{ 7178, { 0xb1, 0xd8, 0x9a, 0x58, 0xc1, 0xf1, 0x17, 0xdd, 0x3d, 0x16, 0x76, 0x78, 0x86, 0x68, 0xbb, 0xd6 } },
	};

	// source file size
	size_t filesize = 13698315;

	// shard the file record resides on
	int shard = 2;

	RaidReq::Params sampleparams(parts[0].serverid, parts[0].hash, parts+1, filesize, 0, filesize, shard, 0, 0);

	RaidReqPoolArray poolarray;
	poolarray.start(1);

	int notifyfd = -1;
	RaidReqPool pool(poolarray);

	currtime = time(NULL);
	RaidReq samplereq(sampleparams, pool, notifyfd);

	FILE* outfp;

	if (!(outfp = fopen("sample.out", "wb")))
	{
		cout << "Can't open sample.out" << endl;
		return 0;
		//exit(0);
	}

	size_t total = 0;
	mtime_t starttime = currtime;
	char buf[16384];

	int success = 1;
	while (total < filesize)
	{
		currtime = time(NULL);
		off_t t = samplereq.readdata(buf, min(sizeof buf, filesize-total));

		if (t < 0)
		{
			cout << "Aborting..." << endl;
			success = 0;
			break;
		}

		total += t;
		cout << "Received " << t << " bytes, total " << total << ", throughput " << total/(currtime-starttime+1) << " bytes/second" << endl;

		if (fwrite(buf, 1, t, outfp) != t)
		{
			cout << "Write error, aborting..." << endl;
			success = 0;
			break;
		}

		//usleep(100000);
	}

	fclose(outfp);

	return success;
}

};