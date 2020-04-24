// Copyright 2019 Xsolla Inc. All Rights Reserved.
// @author Vladimir Alyamkin <ufna@ufna.ru>

#include "XsollaStoreController.h"

#include "XsollaStore.h"
#include "XsollaStoreDataModel.h"
#include "XsollaStoreDefines.h"
#include "XsollaStoreImageLoader.h"
#include "XsollaStoreSave.h"
#include "XsollaStoreSettings.h"

#include "Engine.h"
#include "Engine/DataTable.h"
#include "Json.h"
#include "JsonObjectConverter.h"
#include "Modules/ModuleManager.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "FXsollaStoreModule"

UXsollaStoreController::UXsollaStoreController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> CurrencyLibraryObj(TEXT("DataTable'/Xsolla/Data/currency-format.currency-format'"));
	CurrencyLibrary = CurrencyLibraryObj.Object;

	static ConstructorHelpers::FClassFinder<UUserWidget> BrowserWidgetFinder(TEXT("/Xsolla/Browser/W_StoreBrowser.W_StoreBrowser_C"));
	DefaultBrowserWidgetClass = BrowserWidgetFinder.Class;

	// @TODO https://github.com/xsolla/store-ue4-sdk/issues/68
	CachedCartCurrency = TEXT("USD");
}

void UXsollaStoreController::Initialize(const FString& InProjectId)
{
	ProjectId = InProjectId;

	LoadData();

	// Check image loader is exsits, because initialization can be called multiple times
	if (!ImageLoader)
	{
		ImageLoader = NewObject<UXsollaStoreImageLoader>();
	}
}

void UXsollaStoreController::UpdateVirtualItems(const FOnStoreUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v2/project/%s/items/virtual_items"), *ProjectId);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::GET);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::UpdateVirtualItems_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::UpdateItemGroups(const FString& Locale, const FOnStoreUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	const FString UsedLocale = Locale.IsEmpty() ? TEXT("en") : Locale;
	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v1/project/%s/items/groups?locale=%s"), *ProjectId, *UsedLocale);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::GET);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::UpdateItemGroups_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::UpdateInventory(const FString& AuthToken, const FOnStoreUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	CachedAuthToken = AuthToken;

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v2/project/%s/user/inventory/items"), *ProjectId);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::GET, AuthToken);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::UpdateInventory_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::UpdateVirtualCurrencies(const FOnStoreUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v2/project/%s/items/virtual_currency"), *ProjectId);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::GET);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::UpdateVirtualCurrencies_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::UpdateVirtualCurrencyPackages(const FOnStoreUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v2/project/%s/items/virtual_currency/package"), *ProjectId);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::GET);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::UpdateVirtualCurrencyPackages_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::UpdateVirtualCurrencyBalance(const FString& AuthToken, const FOnStoreUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	CachedAuthToken = AuthToken;

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v2/project/%s/user/virtual_currency_balance"), *ProjectId);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::GET, AuthToken);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::UpdateVirtualCurrencyBalance_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::FetchPaymentToken(const FString& AuthToken, const FString& ItemSKU, const FString& Currency, const FString& Country, const FString& Locale, const FOnFetchTokenSuccess& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	// Prepare request payload
	TSharedPtr<FJsonObject> RequestDataJson = MakeShareable(new FJsonObject);
	if (!Currency.IsEmpty())
		RequestDataJson->SetStringField(TEXT("currency"), Currency);
	if (!Country.IsEmpty())
		RequestDataJson->SetStringField(TEXT("country"), Country);
	if (!Locale.IsEmpty())
		RequestDataJson->SetStringField(TEXT("locale"), Locale);

	RequestDataJson->SetBoolField(TEXT("sandbox"), IsSandboxEnabled());

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v1/project/%s/payment/item/%s"), *ProjectId, *ItemSKU);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::POST, AuthToken, SerializeJson(RequestDataJson));

	const UXsollaStoreSettings* Settings = FXsollaStoreModule::Get().GetSettings();
	if (Settings->bBuildForSteam)
	{
		TSharedPtr<FJsonObject> PayloadJsonObject;
		if (!ParseTokenPayload(AuthToken, PayloadJsonObject))
		{
			UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't parse token payload"), *VA_FUNC_LINE);
			ErrorCallback.ExecuteIfBound(0, 0, TEXT("Can't parse token payload"));
			return;
		}

		FString SteamIdUrl;
		if (!PayloadJsonObject->TryGetStringField(TEXT("id"), SteamIdUrl))
		{
			UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't find Steam profile ID in token payload"), *VA_FUNC_LINE);
			ErrorCallback.ExecuteIfBound(0, 0, TEXT("Can't find Steam profile ID in token payload"));
			return;
		}

		// Extract ID value from user's Steam profile URL
		FString SteamId;
		int SteamIdIndex;
		if (SteamIdUrl.FindLastChar('/', SteamIdIndex))
		{
			SteamId = SteamIdUrl.RightChop(SteamIdIndex + 1);
		}

		HttpRequest->SetHeader(TEXT("x-steam-userid"), SteamId);
	}

	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::FetchPaymentToken_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::FetchCartPaymentToken(const FString& AuthToken, const FString& Currency, const FString& Country, const FString& Locale, const FOnFetchTokenSuccess& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	CachedAuthToken = AuthToken;

	// Prepare request payload
	TSharedPtr<FJsonObject> RequestDataJson = MakeShareable(new FJsonObject);
	if (!Currency.IsEmpty())
		RequestDataJson->SetStringField(TEXT("currency"), Currency);
	if (!Country.IsEmpty())
		RequestDataJson->SetStringField(TEXT("country"), Country);
	if (!Locale.IsEmpty())
		RequestDataJson->SetStringField(TEXT("locale"), Locale);

	RequestDataJson->SetBoolField(TEXT("sandbox"), IsSandboxEnabled());

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v1/project/%s/payment/cart/%d"), *ProjectId, Cart.cart_id);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::POST, AuthToken, SerializeJson(RequestDataJson));

	const UXsollaStoreSettings* Settings = FXsollaStoreModule::Get().GetSettings();
	if (Settings->bBuildForSteam)
	{
		TSharedPtr<FJsonObject> PayloadJsonObject;
		if (!ParseTokenPayload(AuthToken, PayloadJsonObject))
		{
			UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't parse token payload"), *VA_FUNC_LINE);
			ErrorCallback.ExecuteIfBound(0, 0, TEXT("Can't parse token payload"));
			return;
		}

		FString SteamIdUrl;
		if (!PayloadJsonObject->TryGetStringField(TEXT("id"), SteamIdUrl))
		{
			UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't find Steam profile ID in token payload"), *VA_FUNC_LINE);
			ErrorCallback.ExecuteIfBound(0, 0, TEXT("Can't find Steam profile ID in token payload"));
			return;
		}

		// Extract ID value from user's Steam profile URL
		FString SteamId;
		int SteamIdIndex;
		if (SteamIdUrl.FindLastChar('/', SteamIdIndex))
		{
			SteamId = SteamIdUrl.RightChop(SteamIdIndex + 1);
		}

		HttpRequest->SetHeader(TEXT("x-steam-userid"), SteamId);
	}

	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::FetchPaymentToken_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::LaunchPaymentConsole(const FString& AccessToken, UUserWidget*& BrowserWidget)
{
	FString PaystationUrl;
	if (IsSandboxEnabled())
	{
		PaystationUrl = FString::Printf(TEXT("https://sandbox-secure.xsolla.com/paystation3?access_token=%s"), *AccessToken);
	}
	else
	{
		PaystationUrl = FString::Printf(TEXT("https://secure.xsolla.com/paystation3?access_token=%s"), *AccessToken);
	}

	const UXsollaStoreSettings* Settings = FXsollaStoreModule::Get().GetSettings();
	if (Settings->bUsePlatformBrowser)
	{
		UE_LOG(LogXsollaStore, Log, TEXT("%s: Launching Paystation: %s"), *VA_FUNC_LINE, *PaystationUrl);

		BrowserWidget = nullptr;

		FPlatformProcess::LaunchURL(*PaystationUrl, nullptr, nullptr);
	}
	else
	{
		UE_LOG(LogXsollaStore, Log, TEXT("%s: Loading Paystation: %s"), *VA_FUNC_LINE, *PaystationUrl);

		// Check for user browser widget override
		auto BrowserWidgetClass = (Settings->OverrideBrowserWidgetClass) ? Settings->OverrideBrowserWidgetClass : DefaultBrowserWidgetClass;

		PengindPaystationUrl = PaystationUrl;
		auto MyBrowser = CreateWidget<UUserWidget>(GEngine->GameViewport->GetWorld(), BrowserWidgetClass);
		MyBrowser->AddToViewport(MAX_int32);

		BrowserWidget = MyBrowser;
	}
}

void UXsollaStoreController::CheckOrder(const FString& AuthToken, int32 OrderId, const FOnCheckOrder& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	CachedAuthToken = AuthToken;

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v1/project/%s/order/%d"), *ProjectId, OrderId);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::GET, AuthToken);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::CheckOrder_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::CreateCart(const FString& AuthToken, const FOnStoreCartUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	CachedAuthToken = AuthToken;

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v1/project/%s/cart"), *ProjectId);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::POST, AuthToken);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::CreateCart_HttpRequestComplete, SuccessCallback, ErrorCallback);

	CartRequestsQueue.Add(HttpRequest);
	ProcessNextCartRequest();
}

void UXsollaStoreController::ClearCart(const FString& AuthToken, const FOnStoreCartUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	CachedAuthToken = AuthToken;

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v1/project/%s/cart/%d/clear"), *ProjectId, Cart.cart_id);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::PUT, AuthToken);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::ClearCart_HttpRequestComplete, SuccessCallback, ErrorCallback);

	CartRequestsQueue.Add(HttpRequest);
	ProcessNextCartRequest();

	// Just cleanup local cart
	Cart.Items.Empty();
	OnCartUpdate.Broadcast(Cart);
}

void UXsollaStoreController::UpdateCart(const FString& AuthToken, const FOnStoreCartUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	CachedAuthToken = AuthToken;

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v1/project/%s/cart/%d"), *ProjectId, Cart.cart_id);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::GET, AuthToken);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::UpdateCart_HttpRequestComplete, SuccessCallback, ErrorCallback);

	CartRequestsQueue.Add(HttpRequest);
	ProcessNextCartRequest();
}

void UXsollaStoreController::AddToCart(const FString& AuthToken, const FString& ItemSKU, int32 Quantity, const FOnStoreCartUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	CachedAuthToken = AuthToken;

	// Prepare request payload
	TSharedPtr<FJsonObject> RequestDataJson = MakeShareable(new FJsonObject);
	RequestDataJson->SetNumberField(TEXT("quantity"), Quantity);

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v1/project/%s/cart/%d/item/%s"), *ProjectId, Cart.cart_id, *ItemSKU);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::PUT, AuthToken, SerializeJson(RequestDataJson));
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::AddToCart_HttpRequestComplete, SuccessCallback, ErrorCallback);

	CartRequestsQueue.Add(HttpRequest);
	ProcessNextCartRequest();

	// Try to update item quantity
	auto CartItem = Cart.Items.FindByPredicate([ItemSKU](const FStoreCartItem& InItem) {
		return InItem.sku == ItemSKU;
	});

	if (CartItem)
	{
		CartItem->quantity = FMath::Max(0, Quantity);
	}
	else
	{
		auto StoreItem = ItemsData.Items.FindByPredicate([ItemSKU](const FStoreItem& InItem) {
			return InItem.sku == ItemSKU;
		});

		if (StoreItem)
		{
			FStoreCartItem Item(*StoreItem);
			Item.quantity = FMath::Max(0, Quantity);

			// @TODO Predict price locally before cart sync https://github.com/xsolla/store-ue4-sdk/issues/68

			Cart.Items.Add(Item);
		}
		else
		{
			auto CurrencyPackageItem = VirtualCurrencyPackages.Items.FindByPredicate([ItemSKU](const FVirtualCurrencyPackage& InItem) {
				return InItem.sku == ItemSKU;
			});

			if (CurrencyPackageItem)
			{
				FStoreCartItem Item(*CurrencyPackageItem);
				Item.quantity = FMath::Max(0, Quantity);

				Cart.Items.Add(Item);
			}
			else
			{
				UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't find provided SKU in local cache: %s"), *VA_FUNC_LINE, *ItemSKU);
			}
		}
	}

	OnCartUpdate.Broadcast(Cart);
}

void UXsollaStoreController::RemoveFromCart(const FString& AuthToken, const FString& ItemSKU, const FOnStoreCartUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	CachedAuthToken = AuthToken;

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v1/project/%s/cart/%d/item/%s"), *ProjectId, Cart.cart_id, *ItemSKU);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::DELETE, AuthToken);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::RemoveFromCart_HttpRequestComplete, SuccessCallback, ErrorCallback);

	CartRequestsQueue.Add(HttpRequest);
	ProcessNextCartRequest();

	for (int32 i = Cart.Items.Num() - 1; i >= 0; --i)
	{
		if (Cart.Items[i].sku == ItemSKU)
		{
			Cart.Items.RemoveAt(i);
			break;
		}
	}

	OnCartUpdate.Broadcast(Cart);
}

void UXsollaStoreController::ConsumeInventoryItem(const FString& AuthToken, const FString& ItemSKU, int32 Quantity, const FString& InstanceID, const FOnStoreUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	CachedAuthToken = AuthToken;

	// Prepare request payload
	TSharedPtr<FJsonObject> RequestDataJson = MakeShareable(new FJsonObject);
	RequestDataJson->SetStringField(TEXT("sku"), ItemSKU);

	if (Quantity == 0)
	{
		RequestDataJson->SetObjectField(TEXT("quantity"), nullptr);
	}
	else
	{
		RequestDataJson->SetNumberField(TEXT("quantity"), Quantity);
	}

	if (InstanceID.IsEmpty())
	{
		RequestDataJson->SetObjectField(TEXT("instance_id"), nullptr);
	}
	else
	{
		RequestDataJson->SetStringField(TEXT("instance_id"), InstanceID);
	}

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v1/project/%s/user/inventory/item/consume"), *ProjectId);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::POST, AuthToken, SerializeJson(RequestDataJson));
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::ConsumeInventoryItem_HttpRequestComplete, SuccessCallback, ErrorCallback);

	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::GetVirtualCurrency(const FString& CurrencySKU, const FOnCurrencyUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v2/project/%s/items/virtual_currency/sku/%s"), *ProjectId, *CurrencySKU);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::GET);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::GetVirtualCurrency_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::GetVirtualCurrencyPackage(const FString& PackageSKU, const FOnCurrencyPackageUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v2/project/%s/items/virtual_currency/package/sku/%s"), *ProjectId, *PackageSKU);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::GET);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::GetVirtualCurrencyPackage_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::BuyItemWithVirtualCurrency(const FString& AuthToken, const FString& ItemSKU, const FString& CurrencySKU, const FOnPurchaseUpdate& SuccessCallback, const FOnStoreError& ErrorCallback)
{
	CachedAuthToken = AuthToken;

	const FString Url = FString::Printf(TEXT("https://store.xsolla.com/api/v2/project/%s/payment/item/%s/virtual/%s"), *ProjectId, *ItemSKU, *CurrencySKU);

	TSharedRef<IHttpRequest> HttpRequest = CreateHttpRequest(Url, ERequestVerb::POST, AuthToken);
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UXsollaStoreController::BuyItemWithVirtualCurrency_HttpRequestComplete, SuccessCallback, ErrorCallback);
	HttpRequest->ProcessRequest();
}

void UXsollaStoreController::UpdateVirtualItems_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), FStoreItemsData::StaticStruct(), &ItemsData))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't convert server response to struct"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't convert server response to struct"));
		return;
	}

	// Update categories now
	for (auto& Item : ItemsData.Items)
	{
		for (auto& ItemGroup : Item.groups)
		{
			ItemsData.GroupIds.Add(ItemGroup.external_id);
		}
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	SuccessCallback.ExecuteIfBound();
}

void UXsollaStoreController::UpdateItemGroups_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	// Deserialize to new object
	FStoreItemsData GroupsData;
	if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), FStoreItemsData::StaticStruct(), &GroupsData))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't convert server response to struct"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't convert server response to struct"));
		return;
	}

	// Cache data as it should now
	ItemsData.Groups = GroupsData.Groups;

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	SuccessCallback.ExecuteIfBound();
}

void UXsollaStoreController::UpdateInventory_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), FStoreInventory::StaticStruct(), &Inventory))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't convert server response to struct"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't convert server response to struct"));
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	SuccessCallback.ExecuteIfBound();
}

void UXsollaStoreController::UpdateVirtualCurrencies_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), FVirtualCurrencyData::StaticStruct(), &VirtualCurrencyData))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't convert server response to struct"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't convert server response to struct"));
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	SuccessCallback.ExecuteIfBound();
}

void UXsollaStoreController::UpdateVirtualCurrencyPackages_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), FVirtualCurrencyPackagesData::StaticStruct(), &VirtualCurrencyPackages))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't convert server response to struct"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't convert server response to struct"));
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	SuccessCallback.ExecuteIfBound();
}

void UXsollaStoreController::UpdateVirtualCurrencyBalance_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), FVirtualCurrencyBalanceData::StaticStruct(), &VirtualCurrencyBalance))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't convert server response to struct"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't convert server response to struct"));
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	SuccessCallback.ExecuteIfBound();
}

void UXsollaStoreController::FetchPaymentToken_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnFetchTokenSuccess SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	FString AccessToken = JsonObject->GetStringField(TEXT("token"));
	int32 OrderId = JsonObject->GetNumberField(TEXT("order_id"));

	SuccessCallback.ExecuteIfBound(AccessToken, OrderId);
}

void UXsollaStoreController::CheckOrder_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnCheckOrder SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	int32 OrderId = JsonObject->GetNumberField(TEXT("order_id"));
	FString Status = JsonObject->GetStringField(TEXT("status"));
	EXsollaOrderStatus OrderStatus = EXsollaOrderStatus::Unknown;

	if (Status == TEXT("new"))
	{
		OrderStatus = EXsollaOrderStatus::New;
	}
	else if (Status == TEXT("paid"))
	{
		OrderStatus = EXsollaOrderStatus::Paid;
	}
	else if (Status == TEXT("done"))
	{
		OrderStatus = EXsollaOrderStatus::Done;
	}
	else
	{
		UE_LOG(LogXsollaStore, Warning, TEXT("%s: Unknown order status: %s [%d]"), *VA_FUNC_LINE, *Status, OrderId);
	}

	SuccessCallback.ExecuteIfBound(OrderId, OrderStatus);
}

void UXsollaStoreController::CreateCart_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreCartUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		ProcessNextCartRequest();
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	Cart = FStoreCart(JsonObject->GetNumberField(TEXT("id")));
	OnCartUpdate.Broadcast(Cart);

	SaveData();

	SuccessCallback.ExecuteIfBound();
}

void UXsollaStoreController::ClearCart_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreCartUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		ProcessNextCartRequest();
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	SuccessCallback.ExecuteIfBound();

	ProcessNextCartRequest();
}

void UXsollaStoreController::UpdateCart_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreCartUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		ProcessNextCartRequest();
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), FStoreCart::StaticStruct(), &Cart))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't convert server response to struct"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't convert server response to struct"));
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	OnCartUpdate.Broadcast(Cart);

	SuccessCallback.ExecuteIfBound();

	ProcessNextCartRequest();
}

void UXsollaStoreController::AddToCart_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreCartUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		UpdateCart(CachedAuthToken, SuccessCallback, ErrorCallback);
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	SuccessCallback.ExecuteIfBound();

	ProcessNextCartRequest();
}

void UXsollaStoreController::RemoveFromCart_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreCartUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		UpdateCart(CachedAuthToken, SuccessCallback, ErrorCallback);
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	SuccessCallback.ExecuteIfBound();

	ProcessNextCartRequest();
}

void UXsollaStoreController::ConsumeInventoryItem_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	SuccessCallback.ExecuteIfBound();
}

void UXsollaStoreController::GetVirtualCurrency_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnCurrencyUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		ProcessNextCartRequest();
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	FVirtualCurrency currency;
	if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), FVirtualCurrency::StaticStruct(), &currency))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't convert server response to struct"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't convert server response to struct"));
		return;
	}

	SuccessCallback.ExecuteIfBound(currency);
}

void UXsollaStoreController::GetVirtualCurrencyPackage_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnCurrencyPackageUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		ProcessNextCartRequest();
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	FVirtualCurrencyPackage currencyPackage;
	if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), FVirtualCurrencyPackage::StaticStruct(), &currencyPackage))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't convert server response to struct"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't convert server response to struct"));
		return;
	}

	SuccessCallback.ExecuteIfBound(currencyPackage);
}

void UXsollaStoreController::BuyItemWithVirtualCurrency_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnPurchaseUpdate SuccessCallback, FOnStoreError ErrorCallback)
{
	if (HandleRequestError(HttpRequest, HttpResponse, bSucceeded, ErrorCallback))
	{
		return;
	}

	FString ResponseStr = HttpResponse->GetContentAsString();
	UE_LOG(LogXsollaStore, Verbose, TEXT("%s: Response: %s"), *VA_FUNC_LINE, *ResponseStr);

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*HttpResponse->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogXsollaStore, Error, TEXT("%s: Can't deserialize server response"), *VA_FUNC_LINE);
		ErrorCallback.ExecuteIfBound(HttpResponse->GetResponseCode(), 0, TEXT("Can't deserialize server response"));
		return;
	}

	int32 OrderId = JsonObject->GetNumberField(TEXT("order_id"));

	SuccessCallback.ExecuteIfBound(OrderId);
}

bool UXsollaStoreController::HandleRequestError(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnStoreError ErrorCallback)
{
	FString ErrorStr;
	int32 ErrorCode = 0;
	int32 StatusCode = 204;
	FString ResponseStr = TEXT("invalid");

	if (bSucceeded && HttpResponse.IsValid())
	{
		ResponseStr = HttpResponse->GetContentAsString();

		if (!EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()))
		{
			StatusCode = HttpResponse->GetResponseCode();
			ErrorStr = FString::Printf(TEXT("Invalid response. code=%d error=%s"), HttpResponse->GetResponseCode(), *ResponseStr);

			// Example: {"statusCode":403,"errorCode":0,"errorMessage":"Token not found"}
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*ResponseStr);
			if (FJsonSerializer::Deserialize(Reader, JsonObject))
			{
				static const FString ErrorFieldName = TEXT("errorMessage");
				if (JsonObject->HasTypedField<EJson::String>(ErrorFieldName))
				{
					StatusCode = JsonObject->GetNumberField(TEXT("statusCode"));
					ErrorCode = JsonObject->GetNumberField(TEXT("errorCode"));
					ErrorStr = JsonObject->GetStringField(ErrorFieldName);
				}
				else
				{
					ErrorStr = FString::Printf(TEXT("Can't deserialize error json: no field '%s' found"), *ErrorFieldName);
				}
			}
			else
			{
				ErrorStr = TEXT("Can't deserialize error json");
			}
		}
	}
	else
	{
		ErrorStr = TEXT("No response");
	}

	if (!ErrorStr.IsEmpty())
	{
		UE_LOG(LogXsollaStore, Warning, TEXT("%s: request failed (%s): %s"), *VA_FUNC_LINE, *ErrorStr, *ResponseStr);
		ErrorCallback.ExecuteIfBound(StatusCode, ErrorCode, ErrorStr);
		return true;
	}

	return false;
}

void UXsollaStoreController::LoadData()
{
	auto CartData = UXsollaStoreSave::Load();

	CachedCartCurrency = CartData.CartCurrency;
	Cart.cart_id = CartData.CartId;

	OnCartUpdate.Broadcast(Cart);
}

void UXsollaStoreController::SaveData()
{
	UXsollaStoreSave::Save(FXsollaStoreSaveData(Cart.cart_id, CachedCartCurrency));
}

bool UXsollaStoreController::IsSandboxEnabled() const
{
	const UXsollaStoreSettings* Settings = FXsollaStoreModule::Get().GetSettings();
	bool bIsSandboxEnabled = Settings->bSandbox;

#if UE_BUILD_SHIPPING
	bIsSandboxEnabled = Settings->bSandbox && Settings->bEnableSandboxInShipping;
	if (bIsSandboxEnabled)
	{
		UE_LOG(LogXsollaStore, Warning, TEXT("%s: Sandbox should be disabled in Shipping build"), *VA_FUNC_LINE);
	}
#endif // UE_BUILD_SHIPPING

	return bIsSandboxEnabled;
}

TSharedRef<IHttpRequest> UXsollaStoreController::CreateHttpRequest(const FString& Url, const ERequestVerb Verb, const FString& AuthToken, const FString& Content)
{
	TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();

	// Temporal solution with headers processing on server-side #37
	const FString MetaUrl = FString::Printf(TEXT("%sengine=ue4&engine_v=%s&sdk=store&sdk_v=%s"),
		Url.Contains(TEXT("?")) ? TEXT("&") : TEXT("?"),
		ENGINE_VERSION_STRING,
		XSOLLA_STORE_VERSION);
	HttpRequest->SetURL(Url + MetaUrl);

	// Xsolla meta
	HttpRequest->SetHeader(TEXT("X-ENGINE"), TEXT("UE4"));
	HttpRequest->SetHeader(TEXT("X-ENGINE-V"), ENGINE_VERSION_STRING);
	HttpRequest->SetHeader(TEXT("X-SDK"), TEXT("STORE"));
	HttpRequest->SetHeader(TEXT("X-SDK-V"), XSOLLA_STORE_VERSION);

	switch (Verb)
	{
	case ERequestVerb::GET:
		HttpRequest->SetVerb(TEXT("GET"));

		// Check that we doen't provide content with GET request
		if (!Content.IsEmpty())
		{
			UE_LOG(LogXsollaStore, Warning, TEXT("%s: Request content is not empty for GET request. Maybe you should use POST one?"), *VA_FUNC_LINE);
		}
		break;

	case ERequestVerb::POST:
		HttpRequest->SetVerb(TEXT("POST"));
		break;

	case ERequestVerb::PUT:
		HttpRequest->SetVerb(TEXT("PUT"));
		break;

	case ERequestVerb::DELETE:
		HttpRequest->SetVerb(TEXT("DELETE"));
		break;

	default:
		unimplemented();
	}

	if (!AuthToken.IsEmpty())
	{
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AuthToken));
	}

	if (!Content.IsEmpty())
	{
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		HttpRequest->SetContentAsString(Content);
	}

	return HttpRequest;
}

FString UXsollaStoreController::SerializeJson(const TSharedPtr<FJsonObject> DataJson) const
{
	FString JsonContent;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonContent);
	FJsonSerializer::Serialize(DataJson.ToSharedRef(), Writer);
	return JsonContent;
}

bool UXsollaStoreController::ParseTokenPayload(const FString& Token, TSharedPtr<FJsonObject>& PayloadJsonObject) const
{
	TArray<FString> TokenParts;
	Token.ParseIntoArray(TokenParts, TEXT("."));

	FString PayloadStr;
	if (!FBase64::Decode(TokenParts[1], PayloadStr))
	{
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PayloadStr);
	if (!FJsonSerializer::Deserialize(Reader, PayloadJsonObject))
	{
		return false;
	}

	return true;
}

void UXsollaStoreController::ProcessNextCartRequest()
{
	// Cleanup finished requests firts
	int32 CartRequestsNum = CartRequestsQueue.Num();
	for (int32 i = CartRequestsNum - 1; i >= 0; --i)
	{
		if (CartRequestsQueue[i].Get().GetStatus() == EHttpRequestStatus::Succeeded ||
			CartRequestsQueue[i].Get().GetStatus() == EHttpRequestStatus::Failed ||
			CartRequestsQueue[i].Get().GetStatus() == EHttpRequestStatus::Failed_ConnectionError)
		{
			CartRequestsQueue.RemoveAt(i);
		}
	}

	// Check we have request in progress
	bool bRequestInProcess = false;
	for (int32 i = 0; i < CartRequestsQueue.Num(); ++i)
	{
		if (CartRequestsQueue[i].Get().GetStatus() == EHttpRequestStatus::Processing)
		{
			bRequestInProcess = true;
		}
	}

	// Launch next one if we have it
	if (!bRequestInProcess && CartRequestsQueue.Num() > 0)
	{
		CartRequestsQueue[0].Get().ProcessRequest();
	}
}

TArray<FStoreItem> UXsollaStoreController::GetVirtualItems(const FString& GroupFilter) const
{
	if (GroupFilter.IsEmpty())
	{
		return ItemsData.Items;
	}
	else
	{
		return ItemsData.Items.FilterByPredicate([GroupFilter](const FStoreItem& InStoreItem) {
			for (auto& ItemGroup : InStoreItem.groups)
			{
				if (ItemGroup.external_id == GroupFilter)
				{
					return true;
				}
			}
			return false;
		});
	}
}

TArray<FStoreItem> UXsollaStoreController::GetVirtualItemsWithoutGroup() const
{
	return ItemsData.Items.FilterByPredicate([](const FStoreItem& InStoreItem) {
		return InStoreItem.groups.Num() == 0;
	});
}

FStoreItemsData UXsollaStoreController::GetItemsData() const
{
	return ItemsData;
}

TArray<FVirtualCurrency> UXsollaStoreController::GetVirtualCurrencyData() const
{
	return VirtualCurrencyData.Items;
}

TArray<FVirtualCurrencyPackage> UXsollaStoreController::GetVirtualCurrencyPackages() const
{
	return VirtualCurrencyPackages.Items;
}

TArray<FVirtualCurrencyBalance> UXsollaStoreController::GetVirtualCurrencyBalance() const
{
	return VirtualCurrencyBalance.Items;
}

FStoreCart UXsollaStoreController::GetCart() const
{
	return Cart;
}

FStoreInventory UXsollaStoreController::GetInventory() const
{
	return Inventory;
}

FString UXsollaStoreController::GetPendingPaystationUrl() const
{
	return PengindPaystationUrl;
}

UDataTable* UXsollaStoreController::GetCurrencyLibrary() const
{
	return CurrencyLibrary;
}

UXsollaStoreImageLoader* UXsollaStoreController::GetImageLoader() const
{
	return ImageLoader;
}

#undef LOCTEXT_NAMESPACE
