namespace mega
{
	public interface class MRandomNumberProvider
	{
	public:
		void GenerateRandomBlock(Platform::WriteOnlyArray<unsigned char>^ value);
	};
}
