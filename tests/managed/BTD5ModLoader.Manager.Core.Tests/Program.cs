using BTD5ModLoader.Manager.Core;

if (string.IsNullOrWhiteSpace(ProductInfo.Name) ||
    string.IsNullOrWhiteSpace(ProductInfo.Version))
{
    Console.Error.WriteLine("Product metadata smoke test failed.");
    return 1;
}

return 0;
