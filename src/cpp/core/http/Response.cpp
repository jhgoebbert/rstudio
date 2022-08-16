/*
 * Response.cpp
 *
 * Copyright (C) 2022 by Posit, PBC
 *
 * Unless you have received this program directly from Posit pursuant
 * to the terms of a commercial license agreement with Posit, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#include <core/http/Response.hpp>

#include <algorithm>
#include <gsl/gsl>

#include <boost/regex.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/buffer.hpp>

#include <core/http/URL.hpp>
#include <core/http/Util.hpp>
#include <core/http/Cookie.hpp>
#include <shared_core/Hash.hpp>
#include <core/RegexUtils.hpp>
#include <core/FileSerializer.hpp>

#ifndef _WIN32
#include "zlib.h"
#endif

namespace rstudio {
namespace core {
namespace http {

namespace {

#define kGzipWindow 31
#define kDeflateWindow 15
#define kDefaultMemoryUsage 8

std::shared_ptr<StreamBuffer> makePaddingBuffer(size_t numPadding)
{
   // create a padding buffer
   char* buffer = new char[numPadding];
   std::fill_n(buffer, numPadding, '0');

   return std::make_shared<StreamBuffer>(buffer, numPadding);
}

class FileStreamResponse : public StreamResponse
{
   friend class ZlibCompressionStreamResponse;

public:
   FileStreamResponse(const FilePath& file,
                      std::streamsize bufferSize,
                      bool padding) :
      file_(file),
      bufferSize_(bufferSize),
      padding_(padding),
      totalRead_(0)
   {
   }

   virtual ~FileStreamResponse()
   {
   }

   Error initialize()
   {
      return file_.openForRead(fileStream_);
   }

   std::shared_ptr<StreamBuffer> nextBuffer()
   {
      // create buffer to hold the file data
      char* buffer = new char[bufferSize_];

      // read next chunk of data
      fileStream_->read(buffer, bufferSize_);
      uint64_t read = fileStream_->gcount();
      totalRead_ += read;
      fileStream_->seekg(totalRead_);

      if (read == 0)
      {
         delete [] buffer;

         // incomplete read, likely end-of-file reached
         if ((totalRead_ < 1024) && padding_)
         {
            // no data read and we need to pad
            return makePaddingBuffer(1024 - totalRead_);
         }
         else
         {
            // no data read and no need for pad - return empty buffer
            return std::shared_ptr<StreamBuffer>();
         }
      }

      // return buffer representing the data with how much we actually read
      return std::make_shared<StreamBuffer>(buffer, read);
   }

private:
   FilePath file_;
   std::shared_ptr<std::istream> fileStream_;
   std::streamsize bufferSize_;
   bool padding_;

   uint64_t totalRead_;
};

enum class CompressionType
{
   Gzip,
   Deflate
};

#ifndef _WIN32
// we currently do not support direct usage of zlib on windows
class ZlibCompressionStreamResponse : public StreamResponse
{
public:
   ZlibCompressionStreamResponse(const boost::shared_ptr<FileStreamResponse>& fileStream,
                                 std::streamsize bufferSize,
                                 CompressionType compressionType) :
      fileStream_(fileStream),
      bufferSize_(bufferSize),
      compressionType_(compressionType),
      finished_(false)
   {
   }

   virtual ~ZlibCompressionStreamResponse()
   {
   }

   static void freeStream(z_stream* pStream)
   {
      (void)deflateEnd(pStream);
      delete pStream;
   }

   Error initialize()
   {
      Error error = fileStream_->initialize();
      if (error)
         return error;

      // initialize zlib stream
      zStream_.reset(new z_stream(), ZlibCompressionStreamResponse::freeStream);
      zStream_->zalloc = Z_NULL;
      zStream_->zfree = Z_NULL;
      zStream_->opaque = Z_NULL;

      int res = deflateInit2(zStream_.get(),
                             Z_BEST_COMPRESSION,
                             Z_DEFLATED,
                             (compressionType_ == CompressionType::Gzip) ? kGzipWindow : kDeflateWindow,
                             kDefaultMemoryUsage,
                             Z_DEFAULT_STRATEGY);
      if (res != Z_OK)
          return systemError(res, "ZLib initialization error", ERROR_LOCATION);

      return Success();
   }

   std::shared_ptr<StreamBuffer> nextBuffer()
   {
      if (finished_)
         return std::shared_ptr<StreamBuffer>();

      uint64_t written = 0;
      int flush = Z_NO_FLUSH;
      int res = 0;

      // create a new output buffer to hold output generated by zlib
      char* buffer = new char[bufferSize_];
      zStream_->avail_out = bufferSize_;
      zStream_->next_out = reinterpret_cast<unsigned char*>(buffer);

      do
      {
         // check to see if the last file buffer was fully consumed by zlib
         // if not, we need to keep using it
         std::shared_ptr<StreamBuffer> fileBuffer;
         if (fileBuffer_)
         {
            fileBuffer = fileBuffer_;
            fileBuffer_.reset();

            // no change to avail_in or next_in as this is persisted by zlib
            // when reusing the input buffer
         }
         else
         {
            // the buffer was fully consumed last time, so get the next
            // bytes from the file
            fileBuffer = fileStream_->nextBuffer();

            if (!fileBuffer)
            {
               // no more file bytes - signal to zlib that we are done processing
               zStream_->avail_in = 0;
               flush = Z_FINISH;
            }
            else
            {
               // tell zlib about the new input buffer
               zStream_->avail_in = fileBuffer->size;
               zStream_->next_in = reinterpret_cast<unsigned char*>(fileBuffer->data);
            }
         }

         // compress the file bytes
         res = deflate(zStream_.get(), flush);
         if (res == Z_STREAM_ERROR)
         {
            LOG_ERROR_MESSAGE("Could not compress file " + fileStream_->file_.getAbsolutePath() +
                              " - zlib stream error");
            delete [] buffer;

            return std::shared_ptr<StreamBuffer>();
         }

         written = bufferSize_ - zStream_->avail_out;

         if (zStream_->avail_in != 0)
         {
            // the input data has not been fully processed
            // process it on the next call to this method
            fileBuffer_ = fileBuffer;
         }

         // if no data written, zlib isn't ready to give us data
         // keep processing new input data until zlib gives us some output
      } while (written == 0);

      if (res == Z_STREAM_END)
         finished_ = true;

      return std::make_shared<StreamBuffer>(buffer, written);
   }

private:
   boost::shared_ptr<FileStreamResponse> fileStream_;
   std::streamsize bufferSize_;
   CompressionType compressionType_;

   boost::shared_ptr<struct z_stream_s> zStream_;
   std::shared_ptr<StreamBuffer> fileBuffer_;
   bool finished_;
};
#endif

} // anonymous namespace

Response::Response() 
   : Message(), statusCode_(status::Ok) 
{
}   
   
const std::string& Response::statusMessage() const
{ 
   ensureStatusMessage();
   return statusMessage_;
} 

void Response::setStatusMessage(const std::string& statusMessage) 
{
   statusMessage_ = statusMessage;
}
   
std::string Response::contentEncoding() const
{
   return headerValue("Content-Encoding");
}

void Response::setContentEncoding(const std::string& encoding)
{
   setHeader("Content-Encoding", encoding);
}

void Response::setCacheWithRevalidationHeaders()
{
   setHeader("Expires", http::util::httpDate());
   setHeader("Cache-Control", "public, max-age=0, must-revalidate");
}
   
void Response::setCacheForeverHeaders(bool publicAccessiblity)
{
   // set Expires header
   using namespace boost::posix_time;
   time_duration yearDuration = hours(365 * 24);
   ptime expireTime = second_clock::universal_time() + yearDuration;
   setHeader("Expires", http::util::httpDate(expireTime));
   
   // set Cache-Control header
   auto durationSeconds = yearDuration.total_seconds();
   std::string accessibility = publicAccessiblity ? "public" : "private";
   std::string cacheControl(accessibility + ", max-age=" + 
                            safe_convert::numberToString(durationSeconds));
   setHeader("Cache-Control", cacheControl);
}

void Response::setCacheForeverHeaders()
{
   setCacheForeverHeaders(true);
}
   
void Response::setPrivateCacheForeverHeaders()
{
   // NOTE: the Google article referenced above indicates that for the 
   // private scenario you should set the Expires header in the past so 
   // that HTTP 1.0 proxies never cache it. Unfortunately when running 
   // against localhost in Firefox we observed that this prevented Firefox
   // from caching.
   setCacheForeverHeaders(false);
}

// WARNING: This appears to break IE8 if Content-Disposition: attachment
void Response::setNoCacheHeaders()
{
   setHeader("Expires", "Fri, 01 Jan 1990 00:00:00 GMT");
   setHeader("Pragma", "no-cache");
   setHeader("Cache-Control",
             "no-cache, no-store, max-age=0, must-revalidate");
}

void Response::setFrameOptionHeaders(const std::string& options)
{
   std::string option;

   if (options.empty() || options == "none")
   {
      // the default is to deny all framing
      option = "DENY";
   }
   else if (options == "same")
   {
      // this special string indicates that framing is permissible on the same
      // domain
      option = "SAMEORIGIN";
   }
   else
   {
      // the special string "any" means any origin
      if (options != "any")
      {
         option = "ALLOW-FROM " + options;
         
         // Chrome and Safari ignore ALLOW-FROM so also emit Content-Security-Policy
         // https://www.owasp.org/index.php/Clickjacking_Defense_Cheat_Sheet#Defending_with_X-Frame-Options_Response_Headers   
         std::string optionCSP = "frame-ancestors ";
         optionCSP += options;
         setHeader("Content-Security-Policy", optionCSP);
      }
   }

   // multiple space-separated domains not supported by X-Frame-Options, so if 
   // there's a space, don't set the header (modern browsers will use the
   // previously-set Content-Security-Policy)
   if (!option.empty() &&
         boost::algorithm::trim_copy(options).find_first_of(' ') == std::string::npos)
      setHeader("X-Frame-Options", option);
}

// mark this request's user agent compatibility
void Response::setBrowserCompatible(const Request& request)
{
   if (boost::algorithm::contains(request.userAgent(), "Trident"))
      setHeader("X-UA-Compatible", "IE=edge");
}

void Response::addCookie(const Cookie& cookie) 
{
   addHeader("Set-Cookie", cookie.cookieHeaderValue());

   // some browsers may swallow a cookie with SameSite=None
   // so create an additional legacy cookie without SameSite
   // which would be swalllowed by a standard-conforming browser
   if (cookie.sameSite() == Cookie::SameSite::None)
   {
      Cookie legacyCookie = cookie;
      legacyCookie.setName(legacyCookie.name() + kLegacyCookieSuffix);
      legacyCookie.setSameSite(Cookie::SameSite::Undefined);
      addHeader("Set-Cookie", legacyCookie.cookieHeaderValue());
   }
}

 Headers Response::getCookies(const std::vector<std::string>& names /*= {}*/) const
 {
    http::Headers headers;
    for (const http::Header& header : headers_)
    {
       if (header.name == "Set-Cookie")
       {
          if (names.empty())
          {
            headers.push_back(header);
          }
          else
          {
             for (const std::string& name : names)
             {
               if (boost::algorithm::starts_with(header.value, name))
                  headers.push_back(header);
               else if (boost::algorithm::starts_with(header.value, name + kLegacyCookieSuffix))
                  headers.push_back(header);
             }
          }
       }
    }
    return headers;
 }

 void Response::clearCookies()
 {
    for (auto iter = headers_.begin(); iter != headers_.end();)
    {
       if (iter->name == "Set-Cookie")
          iter = headers_.erase(iter);
       else
          ++iter;
    }
 }

Error Response::setBody(const std::string& content)
{
   std::istringstream is(content);
   return setBody(is);
}

Error Response::setCacheableBody(const FilePath& filePath,
                                 const Request& request)
{
   std::string content;
   Error error = core::readStringFromFile(filePath, &content);
   if (error)
      return error;

   return setCacheableBody(content, request);
}

void Response::setDynamicHtml(const std::string& html,
                              const Request& request)
{
   // dynamic html
   setContentType("text/html");
   setNoCacheHeaders();

   // gzip if possible
   if (request.acceptsEncoding(kGzipEncoding))
      setContentEncoding(kGzipEncoding);

   // set body
   setBody(html);
}

void Response::setRangeableFile(const FilePath& filePath,
                                const Request& request)
{
   // read the file in from disk
   std::string contents;
   Error error = core::readStringFromFile(filePath, &contents);
   if (error)
   {
      setError(error);
      return;
   }

   setRangeableFile(contents, filePath.getMimeContentType(), request);
}

void Response::setRangeableFile(const std::string& contents,
                                const std::string& mimeType,
                                const Request& request)
{
   // set content type
   setContentType(mimeType);

   // parse the range field
   std::string range = request.headerValue("Range");
   boost::regex re("bytes=(\\d*)\\-(\\d*)");
   boost::smatch match;
   if (regex_utils::match(range, match, re))
   {
      // specify partial content
      setStatusCode(http::status::PartialContent);

      // determine the byte range
      const size_t kNone = -1;
      size_t begin = safe_convert::stringTo<size_t>(match[1], kNone);
      size_t end = safe_convert::stringTo<size_t>(match[2], kNone);
      size_t total = contents.length();

      if (end == kNone)
      {
         end = total-1;
      }
      if (begin == kNone)
      {
         begin = total - end;
         end = total-1;
      }

      // set the byte range
      addHeader("Accept-Ranges", "bytes");
      boost::format fmt("bytes %1%-%2%/%3%");
      std::string range = boost::str(fmt % begin % end % contents.length());
      addHeader("Content-Range", range);

      // always attempt gzip
      if (request.acceptsEncoding(http::kGzipEncoding))
         setContentEncoding(http::kGzipEncoding);

      // set body
      if (begin == 0 && end == (contents.length()-1))
         setBody(contents);
      else
         setBody(contents.substr(begin, end-begin+1));
   }
   else
   {
      setStatusCode(http::status::RangeNotSatisfiable);
      boost::format fmt("bytes */%1%");
      std::string range = boost::str(fmt % contents.length());
      addHeader("Content-Range", range);
   }
}
   
void Response::setBodyUnencoded(const std::string& body)
{
   removeHeader("Content-Encoding");
   body_ = body;
   setContentLength(gsl::narrow_cast<int>(body_.length()));
}
   
   
void Response::setError(int statusCode, const std::string& message)
{
   setStatusCode(statusCode);
   removeCachingHeaders();
   setContentType("text/html");
   setBodyUnencoded(string_utils::htmlEscape(message));
}

void Response::setNotFoundError(const http::Request& request)
{
   if (notFoundHandler_)
   {
      notFoundHandler_(request, this);
      return;
   }
   else
      setError(http::status::NotFound, request.uri() + " not found");
}

void Response::setNotFoundError(const std::string& uri,
                                const http::Request& request)
{
   // the file this is missing is derived from details in the request,
   // and is not simply the request uri itself
   // as this is a special and rare case, do not attempt to handle it with the
   // not found handler, and simply note which uri was not found
   setError(http::status::NotFound, uri + " not found");
}
   
void Response::setError(const Error& error)
{
   setError(status::InternalServerError, error.getMessage());
}

namespace {

// only take up to the first newline to prevent http response split
std::string safeLocation(const std::string& location)
{
   std::vector<std::string> lines;
   boost::algorithm::split(lines,
                           location,
                           boost::algorithm::is_any_of("\r\n"));
   return lines.size() > 0 ? lines[0] : "";
}

} // anonymous namespace


void Response::setMovedPermanently(const http::Request& request,
                                   const std::string& location)
{
   std::string path = URL(location).protocol() != ""
      ? location
      : request.rootPath() + '/' + safeLocation(location);
   std::string uri = URL::complete(request.baseUri(), path);
   setError(http::status::MovedPermanently, uri);
   setHeader("Location", uri);
}

void Response::setMovedTemporarily(const http::Request& request,
                                   const std::string& location)
{
   std::string path = URL(location).protocol() != ""
      ? location
      : request.rootPath() + '/' + safeLocation(location);
   std::string uri = URL::complete(request.baseUri(), path);
   setError(http::status::MovedTemporarily, uri);
   setHeader("Location", uri);
}
   
void Response::resetMembers() 
{
   statusCode_ = status::Ok;
   statusCodeStr_.clear();
   statusMessage_.clear();
}
   
void Response::removeCachingHeaders()
{
   removeHeader("Expires");
   removeHeader("Pragma");
   removeHeader("Cache-Control");
   removeHeader("Last-Modified");
   removeHeader("ETag");
}
   
std::string Response::eTagForContent(const std::string& content)
{
   return core::hash::crc32Hash(content);
}   

void Response::appendFirstLineBuffers(
      std::vector<boost::asio::const_buffer>& buffers) const 
{
   // create status code string (needs to be a member so memory is still valid
   // for use of buffers)
   std::ostringstream statusCodeStream;
   statusCodeStream << statusCode_;
   statusCodeStr_ = statusCodeStream.str();

   // status line 
   appendHttpVersionBuffers(buffers);
   appendSpaceBuffer(buffers);
   buffers.push_back(boost::asio::buffer(statusCodeStr_));
   appendSpaceBuffer(buffers);
   ensureStatusMessage();
   buffers.push_back(boost::asio::buffer(statusMessage_));
}

namespace status {
namespace Message {
   const char * const SwitchingProtocols = "SwitchingProtocols";
   const char * const Ok = "OK";
   const char * const Created = "Created";
   const char * const PartialContent = "Partial Content";
   const char * const MovedPermanently = "Moved Permanently";
   const char * const MovedTemporarily = "Moved Temporarily";
   const char * const TooManyRedirects = "Too Many Redirects";
   const char * const SeeOther = "See Other";
   const char * const NotModified = "Not Modified";
   const char * const BadRequest = "Bad Request";
   const char * const Unauthorized = "Unauthorized";
   const char * const Forbidden = "Forbidden";
   const char * const NotFound = "Not Found";
   const char * const MethodNotAllowed = "Method Not Allowed";
   const char * const RangeNotSatisfiable = "Range Not Satisfyable";
   const char * const InternalServerError = "Internal Server Error";
   const char * const NotImplemented = "Not Implemented";
   const char * const BadGateway = "Bad Gateway";
   const char * const ServiceUnavailable = "Service Unavailable";
   const char * const GatewayTimeout = "Gateway Timeout";
} // namespace Message
} // namespace status


void Response::ensureStatusMessage() const 
{
   if ( statusMessage_.empty() )
   {
      using namespace status;

      switch(statusCode_)
      {
         case SwitchingProtocols:
            statusMessage_ = status::Message::SwitchingProtocols;
            break;

         case Ok:
            statusMessage_ = status::Message::Ok;
            break;

         case Created:
            statusMessage_ = status::Message::Created;
            break;

         case PartialContent:
            statusMessage_ = status::Message::PartialContent;
            break;

         case MovedPermanently:
            statusMessage_ = status::Message::MovedPermanently;
            break;

         case MovedTemporarily:
            statusMessage_ = status::Message::MovedTemporarily;
            break;

         case TooManyRedirects:
            statusMessage_ = status::Message::TooManyRedirects;
            break;

         case SeeOther:
            statusMessage_ = status::Message::SeeOther;
            break;

         case NotModified:
            statusMessage_ = status::Message::NotModified;
            break;

         case BadRequest:
            statusMessage_ = status::Message::BadRequest;
            break;

         case Unauthorized:
            statusMessage_ = status::Message::Unauthorized;
            break;

         case Forbidden:
            statusMessage_ = status::Message::Forbidden;
            break;

         case NotFound:
            statusMessage_ = status::Message::NotFound;
            break;

         case MethodNotAllowed:
            statusMessage_ = status::Message::MethodNotAllowed;
            break;

         case RangeNotSatisfiable:
            statusMessage_ = status::Message::RangeNotSatisfiable;
            break;

         case InternalServerError:
            statusMessage_ = status::Message::InternalServerError;
            break;

         case NotImplemented:
            statusMessage_ = status::Message::NotImplemented;
            break;

         case BadGateway:
            statusMessage_ = status::Message::BadGateway;
            break;

         case ServiceUnavailable:
            statusMessage_ = status::Message::ServiceUnavailable;
            break;

         case GatewayTimeout:
            statusMessage_ = status::Message::GatewayTimeout;
            break;
      }
   }
}


std::ostream& operator << (std::ostream& stream, const Response& r)
{
   // output status line
   stream << "HTTP/" << r.httpVersionMajor() << "." << r.httpVersionMinor() 
          << " " << r.statusCode() << " " << r.statusMessage() 
          << std::endl;

   // output headers and body
   const Message& m = r;
   stream << m;

   return stream;
}

void Response::setStreamFile(const FilePath& filePath,
                             const Request& request,
                             std::streamsize buffSize)
{
   std::string contentType = filePath.getMimeContentType("application/octet-stream");
   setContentType(contentType);

   // if content type indicates compression, do not compress it again
   // Firefox is unable to handle this case, so we specifically guard against it
   bool compress = (contentType != "application/x-gzip" &&
                    contentType != "application/zip" &&
                    contentType != "application/x-bzip" &&
                    contentType != "application/x-bzip2" &&
                    contentType != "application/x-tar");

   boost::optional<CompressionType> compressionType;

#ifndef _WIN32
   // gzip if possible (never on win32)
   // we prefer the inferior gzip to deflate
   // because older browsers (like IE11) claim to support
   // deflate but in actuality cannot handle it!
   if (request.acceptsEncoding(kGzipEncoding) && compress)
   {
      setContentEncoding(kGzipEncoding);
      compressionType = CompressionType::Gzip;
   }
   else if (request.acceptsEncoding(kDeflateEncoding) && compress)
   {
      setContentEncoding(kDeflateEncoding);
      compressionType = CompressionType::Deflate;
   }
#endif

   // streaming will be performed via chunked encoding
   setHeader(kTransferEncoding, kChunkedTransferEncoding);

   boost::shared_ptr<FileStreamResponse> fileStream(
            new FileStreamResponse(filePath, buffSize, usePadding(request, filePath)));

#ifndef _WIN32
   if (compressionType)
   {
      streamResponse_.reset(
               new ZlibCompressionStreamResponse(fileStream, buffSize, compressionType.get()));
   }
   else
   {
      streamResponse_ = fileStream;
   }
#else
   streamResponse_ = fileStream;
#endif

   Error error = streamResponse_->initialize();
   if (error)
      setError(status::InternalServerError, error.getMessage());
}

} // namespacc http
} // namespace core
} // namespace rstudio

